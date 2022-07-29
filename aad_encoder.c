#include "aad_encoder.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "aad_internal.h"
#include "byte_array.h"
#include "aad_tables.h"

/* エンコード処理ハンドル */
struct AADEncodeProcessor {
  int16_t history[AAD_FILTER_ORDER];  /* 入力データ履歴 */
  int32_t weight[AAD_FILTER_ORDER];   /* フィルタ係数   */
  int32_t quantize_error;             /* 量子化誤差 */
  struct AADTable table;              /* ステップサイズテーブル */
};

/* エンコーダ */
struct AADEncoder {
  struct AADHeaderInfo      header;
  uint8_t                   set_parameter;
  struct AADEncodeProcessor processor[AAD_MAX_NUM_CHANNELS];
  uint8_t                   alloced_by_own;
  uint8_t                   num_encode_trials;
  int32_t                   *input_buffer[AAD_MAX_NUM_CHANNELS];
  int32_t                   *work_buffer[AAD_MAX_NUM_CHANNELS];   /* 作業領域 */
  void                      *work;
};

/* 最大公約数の計算 */
static uint32_t AADEncoder_CalculateGCD(uint32_t a, uint32_t b);

/* 最小公倍数の計算 */
static uint32_t AADEncoder_CalculateLCM(uint32_t a, uint32_t b);

/* エンコード処理ハンドルのリセット */
static void AADEncodeProcessor_Reset(struct AADEncodeProcessor *processor);

/* 1サンプルエンコード */
static uint8_t AADEncodeProcessor_EncodeSample(
    struct AADEncodeProcessor *processor, int32_t sample, uint8_t bits_per_sample);

/* LR -> MS 変換（インターリーブ） */
static void AADEncoder_LRtoMSInterleave(int32_t **buffer, uint32_t num_samples);

/* 単一ブロックのエンコードを試行し、RMSEを計測 */
static AADError AADEncodeProcessor_CalculateRMSError(
    struct AADEncodeProcessor *processor, 
    const int32_t *input, uint32_t num_samples, uint8_t bits_per_sample, double *rmse);

/* 最大性能をもつエンコードプロセッサの探索 */
static AADError AADEncoder_SearchBestProcessor(
    const struct AADEncoder *encoder, 
    const int32_t *const *input, uint32_t progress, uint32_t num_encode_samples,
    struct AADEncodeProcessor *best_processor);

/* 単一データブロックエンコード */
static AADApiResult AADEncoder_EncodeBlock(
    struct AADEncoder *encoder,
    const int32_t *const *input, uint32_t num_samples, 
    uint8_t *data, uint32_t data_size, uint32_t *output_size);

/* エンコードパラメータをヘッダに変換 */
static AADError AADEncoder_ConvertParameterToHeader(
    const struct AADEncodeParameter *enc_param, uint32_t num_samples,
    struct AADHeaderInfo *header_info);

/* 最大公約数の計算 */
static uint32_t AADEncoder_CalculateGCD(uint32_t a, uint32_t b)
{
  AAD_ASSERT((a != 0) && (b != 0));
  if (a % b == 0) {
    return b;
  }
  return AADEncoder_CalculateGCD(b, a % b);
}

/* 最小公倍数の計算 */
static uint32_t AADEncoder_CalculateLCM(uint32_t a, uint32_t b)
{
  AAD_ASSERT((a != 0) && (b != 0));
  return (a * b) / AADEncoder_CalculateGCD(a, b);
}

/* ブロックサイズとブロックあたりサンプル数の計算 */
AADApiResult AADEncoder_CalculateBlockSize(
    uint16_t max_block_size, uint16_t num_channels, uint32_t bits_per_sample,
    uint16_t *block_size, uint32_t *num_samples_per_block)
{
  uint32_t block_data_size;
  uint32_t num_samples_in_block_data;
  uint32_t interleave_data_unit_size;
  uint32_t num_samples_per_interleave_data_unit;

  /* 引数チェック */
  if (block_size == NULL) {
    return AAD_APIRESULT_INVALID_ARGUMENT;
  }

  /* フォーマットチェック */
  if ((num_channels == 0) || (num_channels > AAD_MAX_NUM_CHANNELS)
      || (bits_per_sample == 0) || (bits_per_sample > AAD_MAX_BITS_PER_SAMPLE)) {
    return AAD_APIRESULT_INVALID_FORMAT;
  }

  /* ヘッダが入らない */
  if (max_block_size < AAD_BLOCK_HEADER_SIZE(num_channels)) {
    return AAD_APIRESULT_INVALID_FORMAT;
  }

  /* インターリーブ配置したデータ単位サイズ[byte] */
  interleave_data_unit_size = num_channels * (AADEncoder_CalculateLCM(8, bits_per_sample) / 8);
  num_samples_per_interleave_data_unit = (interleave_data_unit_size * 8) / (num_channels * bits_per_sample);

  /* ブロックデータサイズの計算 */
  block_data_size = (uint32_t)(max_block_size - AAD_BLOCK_HEADER_SIZE(num_channels));
  /* データ単位サイズに切り捨て */
  block_data_size = interleave_data_unit_size * (block_data_size / interleave_data_unit_size);

  /* ブロックデータ内に入れられるサンプル数を計算 */
  num_samples_in_block_data
    = num_samples_per_interleave_data_unit * (block_data_size / interleave_data_unit_size);
  
  /* ブロックサイズの確定 */
  AAD_ASSERT((uint32_t)AAD_BLOCK_HEADER_SIZE(num_channels) + block_data_size <= UINT16_MAX);
  (*block_size) = (uint16_t)((uint16_t)AAD_BLOCK_HEADER_SIZE(num_channels) + block_data_size);
  /* ヘッダに入っている分を加算する */
  if (num_samples_per_block != NULL) {
    (*num_samples_per_block) = num_samples_in_block_data + AAD_FILTER_ORDER;
  }
  return AAD_APIRESULT_OK;
}

/* ヘッダエンコード */
AADApiResult AADEncoder_EncodeHeader(
    const struct AADHeaderInfo *header_info, uint8_t *data, uint32_t data_size)
{
  uint8_t *data_pos;

  /* 引数チェック */
  if ((header_info == NULL) || (data == NULL)) {
    return AAD_APIRESULT_INVALID_ARGUMENT;
  }

  /* ヘッダサイズと入力データサイズの比較 */
  if (data_size < AAD_HEADER_SIZE) {
    return AAD_APIRESULT_INSUFFICIENT_DATA;
  }

  /* ヘッダチェック */
  /* データに書き出す（副作用）前にできる限りのチェックを行う */
  /* チャンネル数 */
  if ((header_info->num_channels == 0) 
      || (header_info->num_channels > AAD_MAX_NUM_CHANNELS)) {
    return AAD_APIRESULT_INVALID_FORMAT;
  }
  /* サンプル数 */
  if (header_info->num_samples == 0) {
    return AAD_APIRESULT_INVALID_FORMAT;
  }
  /* サンプリングレート */
  if (header_info->sampling_rate == 0) {
    return AAD_APIRESULT_INVALID_FORMAT;
  }
  /* ビット深度 */
  if ((header_info->bits_per_sample > AAD_MAX_BITS_PER_SAMPLE)
      || (header_info->bits_per_sample < AAD_MIN_BITS_PER_SAMPLE)) {
    return AAD_APIRESULT_INVALID_FORMAT;
  }
  /* ブロックサイズ */
  if (header_info->block_size <= AAD_BLOCK_HEADER_SIZE(header_info->num_channels)) {
    return AAD_APIRESULT_INVALID_FORMAT;
  }
  /* ブロックあたりサンプル数 */
  if (header_info->num_samples_per_block == 0) {
    return AAD_APIRESULT_INVALID_FORMAT;
  }
  /* マルチチャンネル処理法 */
  if (header_info->ch_process_method >= AAD_CH_PROCESS_METHOD_INVALID) {
    return AAD_APIRESULT_INVALID_FORMAT;
  }
  /* モノラルではMS処理はできない */
  if ((header_info->ch_process_method == AAD_CH_PROCESS_METHOD_MS) 
      && (header_info->num_channels == 1)) {
    return AAD_APIRESULT_INVALID_FORMAT;
  }

  /* 書き出し用ポインタ設定 */
  data_pos = data;

  /* シグネチャ */
  ByteArray_PutUint8(data_pos, 'A');
  ByteArray_PutUint8(data_pos, 'A');
  ByteArray_PutUint8(data_pos, 'D');
  ByteArray_PutUint8(data_pos, '\0');
  /* フォーマットバージョン
   * 補足）ヘッダの設定値は無視してマクロ値を書き込む */
  ByteArray_PutUint32BE(data_pos, AAD_FORMAT_VERSION);
  /* コーデックバージョン
   * 補足）ヘッダの設定値は無視してマクロ値を書き込む */
  ByteArray_PutUint32BE(data_pos, AAD_CODEC_VERSION);
  /* チャンネル数 */
  ByteArray_PutUint16BE(data_pos, header_info->num_channels);
  /* サンプル数 */
  ByteArray_PutUint32BE(data_pos, header_info->num_samples);
  /* サンプリングレート */
  ByteArray_PutUint32BE(data_pos, header_info->sampling_rate);
  /* サンプルあたりビット数 */
  ByteArray_PutUint16BE(data_pos, header_info->bits_per_sample);
  /* ブロックサイズ */
  ByteArray_PutUint16BE(data_pos, header_info->block_size);
  /* ブロックあたりサンプル数 */
  ByteArray_PutUint32BE(data_pos, header_info->num_samples_per_block);
  /* マルチチャンネル処理法 */
  ByteArray_PutUint8(data_pos, header_info->ch_process_method);

  /* ヘッダサイズチェック */
  AAD_ASSERT((data_pos - data) == AAD_HEADER_SIZE);

  /* 成功終了 */
  return AAD_APIRESULT_OK;
}

/* エンコーダワークサイズ計算 */
int32_t AADEncoder_CalculateWorkSize(uint16_t max_block_size)
{
  int32_t work_size;
  uint32_t num_samples_per_block;
  uint16_t block_size;

  /* 最大ブロックサイズから最大のブロックあたりのサンプル数を計算 */
  /* note:最もサンプル数が入るケースとして、ビット数は最小かつチャンネル数は1とする */
  if (AADEncoder_CalculateBlockSize(
        max_block_size, 1, AAD_MIN_BITS_PER_SAMPLE,
        &block_size, &num_samples_per_block) != AAD_APIRESULT_OK) {
    return -1;
  }

  /* 構造体サイズ */
  work_size = AAD_ALIGNMENT + sizeof(struct AADEncoder);

  /* バッファサイズ: 作業領域用に2倍確保 */
  work_size += 2 * AAD_MAX_NUM_CHANNELS * (int32_t)(sizeof(int32_t) * num_samples_per_block + AAD_ALIGNMENT);

  return work_size;
}

/* エンコーダハンドル作成 */
struct AADEncoder *AADEncoder_Create(uint16_t max_block_size, void *work, int32_t work_size)
{
  uint32_t ch;
  struct AADEncoder *encoder;
  uint8_t *work_ptr;
  uint8_t tmp_alloced_by_own = 0;
  uint16_t block_size;
  uint32_t num_samples_per_block;

  /* ブロックあたりサンプル数の計算 */
  /* note:最もサンプル数が入るケースとして、ビット数は最小かつチャンネル数は1とする */
  if (AADEncoder_CalculateBlockSize(
        max_block_size, 1, AAD_MIN_BITS_PER_SAMPLE,
        &block_size, &num_samples_per_block) != AAD_APIRESULT_OK) {
    return NULL;
  }

  /* 領域自前確保の場合 */
  if ((work == NULL) && (work_size == 0)) {
    if ((work_size = AADEncoder_CalculateWorkSize(max_block_size)) < 0) {
      return NULL;
    }
    work = malloc((size_t)work_size);
    tmp_alloced_by_own = 1;
  }

  /* 引数チェック */
  if ((work == NULL) || (work_size < AADEncoder_CalculateWorkSize(max_block_size))) {
    return NULL;
  }

  work_ptr = (uint8_t *)work;

  /* アラインメントを揃えてから構造体を配置 */
  work_ptr = (uint8_t *)AAD_ROUND_UP((uintptr_t)work_ptr, AAD_ALIGNMENT);
  encoder = (struct AADEncoder *)work_ptr;
  work_ptr += sizeof(struct AADEncoder);

  /* バッファ領域の確保 */
  for (ch = 0; ch < AAD_MAX_NUM_CHANNELS; ch++) {
    work_ptr = (uint8_t *)AAD_ROUND_UP((uintptr_t)work_ptr, AAD_ALIGNMENT);
    encoder->input_buffer[ch] = (int32_t *)work_ptr;
    work_ptr += sizeof(int32_t) * num_samples_per_block;
  }
  for (ch = 0; ch < AAD_MAX_NUM_CHANNELS; ch++) {
    work_ptr = (uint8_t *)AAD_ROUND_UP((uintptr_t)work_ptr, AAD_ALIGNMENT);
    encoder->work_buffer[ch] = (int32_t *)work_ptr;
    work_ptr += sizeof(int32_t) * num_samples_per_block;
  }

  /* エンコード処理ハンドルのリセット */
  for (ch = 0; ch < AAD_MAX_NUM_CHANNELS; ch++) {
    AADEncodeProcessor_Reset(&(encoder->processor[ch]));
  }

  /* パラメータは未セット状態に */
  encoder->set_parameter = 0;

  /* メモリ先頭アドレスを記録 */
  encoder->work = work;

  /* 自前確保であることをマーク */
  encoder->alloced_by_own = tmp_alloced_by_own;

  /* バッファオーバーランチェック */
  AAD_ASSERT((int32_t)(work_ptr - (uint8_t *)work) <= work_size);

  return encoder;
}

/* エンコーダハンドル破棄 */
void AADEncoder_Destroy(struct AADEncoder *encoder)
{
  if (encoder != NULL) {
    /* 自分で領域確保していたら破棄 */
    if (encoder->alloced_by_own == 1) {
      free(encoder->work);
    }
  }
}

/* エンコード処理ハンドルのリセット */
static void AADEncodeProcessor_Reset(struct AADEncodeProcessor *processor)
{
  uint32_t i;

  AAD_ASSERT(processor != NULL);

  for (i = 0; i < AAD_FILTER_ORDER; i++) {
    processor->history[i] = 0;
    processor->weight[i] = 0;
  }
}

/* 1サンプルエンコード */
static uint8_t AADEncodeProcessor_EncodeSample(
    struct AADEncodeProcessor *processor, int32_t sample, uint8_t bits_per_sample)
{
  uint8_t code;
  int32_t predict, diff, qdiff, delta, stepsize, diffabs, sign;
  int32_t quantize_sample, ord;
  const uint8_t signbit = (uint8_t)(1U << (bits_per_sample - 1));
  const uint8_t absmask = (uint8_t)(signbit - 1);

  AAD_ASSERT(processor != NULL);
  AAD_ASSERT((bits_per_sample >= AAD_MIN_BITS_PER_SAMPLE) && (bits_per_sample <= AAD_MAX_BITS_PER_SAMPLE));

  /* ステップサイズの取得 */
  stepsize = AADTable_GetStepSize(&(processor->table));

  /* フィルタ予測 */
  predict = AAD_FIXEDPOINT_0_5;
  for (ord = 0; ord < AAD_FILTER_ORDER; ord++) {
    predict += processor->history[ord] * processor->weight[ord];
  }
  predict >>= AAD_FIXEDPOINT_DIGITS;

  /* 差分 */
  diff = sample - predict;
  sign = diff < 0;
  diffabs = sign ? -diff : diff;

  /* 差分を符号表現に変換 */
  /* code = sign(diff) * round(|diff| * 2**(bits_per_sample-2) / stepsize) */
  code = (uint8_t)AAD_MIN_VAL((diffabs << (bits_per_sample - 2)) / stepsize, absmask);
  /* codeの最上位ビットは符号ビット */
  if (sign) {
    code |= signbit;
  }

  /* 量子化した差分を計算 */
  /* diff = stepsize * (delta + 0.5) / 2**(bits_per_sample-2) */
  /* -> diff = stepsize * (delta * 2 + 1) / 2**(bits_per_sample-1) */
  delta = code & absmask;
  qdiff = (stepsize * ((delta << 1) + 1)) >> (bits_per_sample - 1);
  qdiff = (sign) ? -qdiff : qdiff; /* 符号ビットの反映 */

  /* インデックス更新 */
  AADTable_UpdateIndex(&(processor->table), code);

  /* 計算結果の反映 */
  processor->quantize_error = qdiff;

  /* 量子化後のサンプル値 */
  quantize_sample = qdiff + predict;
  /* 16bit幅にクリップ */
  quantize_sample = AAD_INNER_VAL(quantize_sample, INT16_MIN, INT16_MAX);

  /* フィルタ係数更新 */
  for (ord = 0; ord < AAD_FILTER_ORDER; ord++) {
    processor->weight[ord]
      += (qdiff * processor->history[ord] + AAD_FIXEDPOINT_0_5) >> (AAD_FIXEDPOINT_DIGITS + AAD_LMSFILTER_SHIFT);
  }

  /* 入力データ履歴更新 */
  for (ord = AAD_FILTER_ORDER - 1; ord > 0; ord--) {
    processor->history[ord] = processor->history[ord - 1];
  }
  processor->history[0] = (int16_t)quantize_sample;

  AAD_ASSERT(code <= AAD_MAX_CODE_VALUE);
  return code;
}

/* LR -> MS 変換（インターリーブ） */
static void AADEncoder_LRtoMSInterleave(int32_t **buffer, uint32_t num_samples)
{
  uint32_t smpl;
  int32_t mid, side;

  AAD_ASSERT(buffer != NULL);
  AAD_ASSERT((buffer[0] != NULL) && (buffer[1] != NULL));

  /* 音が割れて誤差が増大するのを防ぐため、変換時に右シフト */
  for (smpl = 0; smpl < num_samples; smpl++) {
    mid  = (buffer[0][smpl] + buffer[1][smpl]) >> 1;
    side = (buffer[0][smpl] - buffer[1][smpl]) >> 1;
    buffer[0][smpl] = AAD_INNER_VAL(mid,  INT16_MIN, INT16_MAX);
    buffer[1][smpl] = AAD_INNER_VAL(side, INT16_MIN, INT16_MAX);
  }
}

/* 単一ブロックのエンコードを試行し、RMSEを計測 */
static AADError AADEncodeProcessor_CalculateRMSError(
    struct AADEncodeProcessor *processor, 
    const int32_t *input, uint32_t num_samples, uint8_t bits_per_sample, double *rmse)
{
  uint32_t smpl;
  double sum_squared_error;

  /* 引数チェック */
  if ((processor == NULL) || (input == NULL) || (rmse == NULL)) {
    return AAD_ERROR_INVALID_ARGUMENT;
  }

  /* サンプル数が少なすぎるときは誤差0とする（先頭サンプルで正確に予測できるから） */
  if (num_samples < AAD_FILTER_ORDER) {
    (*rmse) = 0.0f;
    return AAD_ERROR_OK;
  }

  /* EncodeBlockに倣い、フィルタに先頭サンプルをセット */
  for (smpl = 0; smpl < AAD_FILTER_ORDER; smpl++) {
    AAD_ASSERT(input[smpl] <= INT16_MAX); AAD_ASSERT(input[smpl] >= INT16_MIN);
    processor->history[AAD_FILTER_ORDER - smpl - 1] = (int16_t)input[smpl];
  }

  /* 誤差計測 */
  sum_squared_error = 0.0f;
  for (smpl = AAD_FILTER_ORDER; smpl < num_samples; smpl++) {
    /* サンプルエンコードを実行し状態更新 エンコード結果は捨てる */
    AADEncodeProcessor_EncodeSample(processor, input[smpl], bits_per_sample); 
    /* 量子化後の誤差を累積 */
    sum_squared_error += processor->quantize_error * processor->quantize_error;
  }

  /* RMSEに変換 */
  (*rmse) = sqrt(sum_squared_error / num_samples);
  return AAD_ERROR_OK;
}

/* 最大性能をもつエンコードプロセッサの探索 */
static AADError AADEncoder_SearchBestProcessor(
    const struct AADEncoder *encoder, 
    const int32_t *const *input, uint32_t progress, uint32_t num_encode_samples,
    struct AADEncodeProcessor *best_processor)
{
  uint32_t ch, trial;
  double min_rmse[AAD_MAX_NUM_CHANNELS];
  struct AADEncodeProcessor tmp_best[AAD_MAX_NUM_CHANNELS];
  int32_t *buffer[AAD_MAX_NUM_CHANNELS];
  int32_t *prev_buffer[AAD_MAX_NUM_CHANNELS];
  const struct AADHeaderInfo *header;
  AADError err;

  /* 引数チェック */
  if ((encoder == NULL) || (input == NULL) || (best_processor == NULL)) {
    return AAD_ERROR_INVALID_ARGUMENT;
  }
  header = &(encoder->header);

  /* 入力をバッファにコピー */
  for (ch = 0; ch < header->num_channels; ch++) {
    /* ポインタ取得 */
    buffer[ch] = encoder->input_buffer[ch];
    memcpy(buffer[ch], &input[ch][progress], sizeof(int32_t) * num_encode_samples);
  }

  /* LR -> MS */
  if ((header->num_channels >= 2)
      && (header->ch_process_method == AAD_CH_PROCESS_METHOD_MS)) {
    AADEncoder_LRtoMSInterleave(buffer, num_encode_samples);
  }

  /* 直前のブロックのデータを準備 */
  if (progress >= header->num_samples_per_block) {
    for (ch = 0; ch < header->num_channels; ch++) {
      prev_buffer[ch] = encoder->work_buffer[ch];
      memcpy(prev_buffer[ch], &input[ch][progress - header->num_samples_per_block], sizeof(int32_t) * header->num_samples_per_block);
    }
    if ((header->num_channels >= 2)
        && (header->ch_process_method == AAD_CH_PROCESS_METHOD_MS)) {
      AADEncoder_LRtoMSInterleave(prev_buffer, header->num_samples_per_block);
    }
  }

  /* プロセッサを取得 */
  memcpy(&tmp_best, encoder->processor, sizeof(struct AADEncodeProcessor) * header->num_channels);

  /* 何もしないときの基準値を計測 */
  for (ch = 0; ch < header->num_channels; ch++) { 
    struct AADEncodeProcessor tmp_processor = encoder->processor[ch];
    if ((err = AADEncodeProcessor_CalculateRMSError(
          &tmp_processor, buffer[ch],
          num_encode_samples, (uint8_t)header->bits_per_sample, &min_rmse[ch])) != AAD_ERROR_OK) {
      return err;
    }
  }

  /* 連続したブロックを複数回エンコードし、最小のRMSEを持つプロセッサを探す */
  /* memo: 複数回エンコードすることでフィルタの適応が早まる。
   * ただし、繰り返した分だけ単調に誤差が小さくなるとは限らないため（過学習など）、最も誤差の小さいプロセッサを採用 */
  for (ch = 0; ch < header->num_channels; ch++) {
    struct AADEncodeProcessor tmp_processor = encoder->processor[ch];
    struct AADEncodeProcessor candidate;
    for (trial = 0; trial < encoder->num_encode_trials; trial++) {
      double tmp_rmse;
      /* 直前のブロック */
      if (progress >= header->num_samples_per_block) {
        if ((err = AADEncodeProcessor_CalculateRMSError(
                &tmp_processor, prev_buffer[ch], 
                header->num_samples_per_block, (uint8_t)header->bits_per_sample, &tmp_rmse)) != AAD_ERROR_OK) {
          return err;
        }
      }
      /* 採用候補のプロセッサはここでの設定値を用いる */
      candidate = tmp_processor;
      /* エンコード対象のブロックのRMSEを計測 */
      if ((err = AADEncodeProcessor_CalculateRMSError(
            &tmp_processor, buffer[ch], 
            num_encode_samples, (uint8_t)header->bits_per_sample, &tmp_rmse)) != AAD_ERROR_OK) {
        return err;
      }
      /* RMSE基準でプロセッサを選択 */
      if (min_rmse[ch] > tmp_rmse) {
        min_rmse[ch] = tmp_rmse;
        tmp_best[ch] = candidate;
      }
    }
  }

  /* 最善プロセッサの記録 */
  memcpy(best_processor, &tmp_best, sizeof(struct AADEncodeProcessor) * header->num_channels);
  return AAD_ERROR_OK;
}

/* 単一データブロックエンコード */
static AADApiResult AADEncoder_EncodeBlock(
    struct AADEncoder *encoder,
    const int32_t *const *input, uint32_t num_samples, 
    uint8_t *data, uint32_t data_size, uint32_t *output_size)
{
  const struct AADHeaderInfo *header;
  uint32_t ch, smpl;
  uint8_t *data_pos;
  int32_t *buffer[AAD_MAX_NUM_CHANNELS];

  AAD_ASSERT(num_samples <= encoder->header.num_samples_per_block);

  /* 引数チェック */
  if ((encoder == NULL) || (data == NULL)
      || (input == NULL) || (output_size == NULL)) {
    return AAD_APIRESULT_INVALID_ARGUMENT;
  }
  header = &(encoder->header);

  /* 書き出しポインタのセット */
  data_pos = data;

  /* 入力をバッファにコピー */
  for (ch = 0; ch < header->num_channels; ch++) {
    /* ポインタ取得 */
    buffer[ch] = encoder->input_buffer[ch];
    /* バッファの末尾に前回エンコードの残骸が残る場合があるので、ブロックの大きさで0クリア */
    memset(buffer[ch], 0, sizeof(int32_t) * header->num_samples_per_block);
    memcpy(buffer[ch], input[ch], sizeof(int32_t) * num_samples);
  }

  /* LR -> MS */
  if (header->ch_process_method == AAD_CH_PROCESS_METHOD_MS) {
    /* チャンネル数チェック */
    if (header->num_channels < 2) {
      return AAD_APIRESULT_INVALID_FORMAT;
    }
    AADEncoder_LRtoMSInterleave(buffer, num_samples);
  }

  /* フィルタに先頭サンプルをセット */
  for (ch = 0; ch < header->num_channels; ch++) {
    /* 総サンプル数がフィルタ次数より少ない場合がある */
    uint32_t num_buffer = AAD_MIN_VAL(AAD_FILTER_ORDER, num_samples);
    for (smpl = 0; smpl < AAD_FILTER_ORDER; smpl++) {
      encoder->processor[ch].history[AAD_FILTER_ORDER - smpl - 1] = 0;
      if (smpl < num_buffer) {
        AAD_ASSERT(buffer[ch][smpl] <= INT16_MAX); AAD_ASSERT(buffer[ch][smpl] >= INT16_MIN);
        encoder->processor[ch].history[AAD_FILTER_ORDER - smpl - 1] = (int16_t)buffer[ch][smpl];
      }
    }
  }

  /* ブロックヘッダエンコード */
  for (ch = 0; ch < header->num_channels; ch++) {
    /* シフト量の計算と右シフト */
    uint8_t shift;
    uint16_t u16buf;
    int32_t maxabs = 0, mask;
    /* 最大の係数絶対値の探索 */
    for (smpl = 0; smpl < AAD_FILTER_ORDER; smpl++) {
      int32_t abs = AAD_ABS_VAL(encoder->processor[ch].weight[smpl]);
      if (maxabs < abs) {
        maxabs = abs;
      }
    }
    /* 最大値が16bit幅に収まる右シフト量をサーチ */
    shift = 0;
    while (maxabs > INT16_MAX) {
      maxabs >>= 1;
      shift++;
    }
    /* 係数シフトによる丸め（シフトしたビットは0にクリア） */
    mask = ~((1 << shift) - 1);
    for (smpl = 0; smpl < AAD_FILTER_ORDER; smpl++) {
      encoder->processor[ch].weight[smpl] &= mask;
    }
    /* ステップサイズインデックス12bit + 係数シフト量4bit */
    AAD_STATIC_ASSERT(AAD_TABLES_FLOAT_DIGITS == 4);
    AAD_ASSERT(shift <= 0xF);
    u16buf = (uint16_t)(encoder->processor[ch].table.stepsize_index << AAD_TABLES_FLOAT_DIGITS);
    u16buf = (uint16_t)(u16buf | (shift & 0xF));
    ByteArray_PutUint16BE(data_pos, u16buf);
    /* フィルタの状態を出力 係数はシフトして記録 */
    for (smpl = 0; smpl < AAD_FILTER_ORDER; smpl++) {
      AAD_ASSERT((encoder->processor[ch].weight[smpl] >> shift) <= INT16_MAX);
      AAD_ASSERT((encoder->processor[ch].weight[smpl] >> shift) >= INT16_MIN);
      ByteArray_PutUint16BE(data_pos, (uint16_t)(encoder->processor[ch].weight[smpl] >> shift));
      ByteArray_PutUint16BE(data_pos, encoder->processor[ch].history[smpl]);
    }
  }

  /* ブロックヘッダサイズチェック */
  AAD_ASSERT((uint32_t)(data_pos - data) == AAD_BLOCK_HEADER_SIZE(header->num_channels));

  /* データエンコード */
  switch (header->bits_per_sample) {
    case 4:
      for (smpl = AAD_FILTER_ORDER; smpl < num_samples; smpl += 2) {
        uint8_t code[2];
        for (ch = 0; ch < header->num_channels; ch++) {
          AAD_ASSERT((uint32_t)(data_pos - data) < data_size);
          AAD_ASSERT((uint32_t)(data_pos - data) < header->block_size);
          code[0] = AADEncodeProcessor_EncodeSample(&(encoder->processor[ch]), buffer[ch][smpl + 0], 4);
          code[1] = AADEncodeProcessor_EncodeSample(&(encoder->processor[ch]), buffer[ch][smpl + 1], 4);
          AAD_ASSERT((code[0] <= 0xF) && (code[1] <= 0xF));
          ByteArray_PutUint8(data_pos, (code[0] << 4) | code[1]);
          AAD_ASSERT((uint32_t)(data_pos - data) <= data_size);
          AAD_ASSERT((uint32_t)(data_pos - data) <= header->block_size);
        }
      }
      break;
    case 3:
      for (smpl = AAD_FILTER_ORDER; smpl < num_samples; smpl += 8) {
        uint8_t code[8];
        uint32_t outbuf;
        for (ch = 0; ch < header->num_channels; ch++) {
          AAD_ASSERT((uint32_t)(data_pos - data) < data_size);
          AAD_ASSERT((uint32_t)(data_pos - data) < header->block_size);
          code[0] = AADEncodeProcessor_EncodeSample(&(encoder->processor[ch]), buffer[ch][smpl + 0], 3);
          code[1] = AADEncodeProcessor_EncodeSample(&(encoder->processor[ch]), buffer[ch][smpl + 1], 3);
          code[2] = AADEncodeProcessor_EncodeSample(&(encoder->processor[ch]), buffer[ch][smpl + 2], 3);
          code[3] = AADEncodeProcessor_EncodeSample(&(encoder->processor[ch]), buffer[ch][smpl + 3], 3);
          code[4] = AADEncodeProcessor_EncodeSample(&(encoder->processor[ch]), buffer[ch][smpl + 4], 3);
          code[5] = AADEncodeProcessor_EncodeSample(&(encoder->processor[ch]), buffer[ch][smpl + 5], 3);
          code[6] = AADEncodeProcessor_EncodeSample(&(encoder->processor[ch]), buffer[ch][smpl + 6], 3);
          code[7] = AADEncodeProcessor_EncodeSample(&(encoder->processor[ch]), buffer[ch][smpl + 7], 3);
          AAD_ASSERT((code[0] <= 0x7) && (code[1] <= 0x7) && (code[2] <= 0x7) && (code[3] <= 0x7)
                  && (code[4] <= 0x7) && (code[5] <= 0x7) && (code[6] <= 0x7) && (code[7] <= 0x7));
          /* 3byteに詰める */
          outbuf = (uint32_t)((code[0] << 21) | (code[1] << 18) | (code[2] << 15) | (code[3] << 12)
                            | (code[4] <<  9) | (code[5] <<  6) | (code[6] <<  3) | (code[7] <<  0));
          ByteArray_PutUint24BE(data_pos, outbuf);
          AAD_ASSERT((uint32_t)(data_pos - data) <= data_size);
          AAD_ASSERT((uint32_t)(data_pos - data) <= header->block_size);
        }
      }
      break;
    case 2:
      for (smpl = AAD_FILTER_ORDER; smpl < num_samples; smpl += 4) {
        uint8_t code[4];
        for (ch = 0; ch < header->num_channels; ch++) {
          AAD_ASSERT((uint32_t)(data_pos - data) < data_size);
          AAD_ASSERT((uint32_t)(data_pos - data) < header->block_size);
          code[0] = AADEncodeProcessor_EncodeSample(&(encoder->processor[ch]), buffer[ch][smpl + 0], 2);
          code[1] = AADEncodeProcessor_EncodeSample(&(encoder->processor[ch]), buffer[ch][smpl + 1], 2);
          code[2] = AADEncodeProcessor_EncodeSample(&(encoder->processor[ch]), buffer[ch][smpl + 2], 2);
          code[3] = AADEncodeProcessor_EncodeSample(&(encoder->processor[ch]), buffer[ch][smpl + 3], 2);
          AAD_ASSERT((code[0] <= 0x3) && (code[1] <= 0x3) && (code[2] <= 0x3) && (code[3] <= 0x3));
          ByteArray_PutUint8(data_pos, (code[0] << 6) | (code[1] << 4) | (code[2] << 2) | ((code[3] << 0)));
          AAD_ASSERT((uint32_t)(data_pos - data) <= data_size);
          AAD_ASSERT((uint32_t)(data_pos - data) <= header->block_size);
        }
      }
      break;
    default:
      return AAD_APIRESULT_INVALID_FORMAT;
  }

  /* 成功終了 */
  (*output_size) = (uint32_t)(data_pos - data);
  return AAD_APIRESULT_OK;
}

/* エンコードパラメータをヘッダに変換 */
static AADError AADEncoder_ConvertParameterToHeader(
    const struct AADEncodeParameter *enc_param, uint32_t num_samples,
    struct AADHeaderInfo *header_info)
{
  struct AADHeaderInfo tmp_header = { 0, };

  /* 引数チェック */
  if ((enc_param == NULL) || (header_info == NULL)) {
    return AAD_ERROR_INVALID_ARGUMENT;
  }

  /* パラメータのチェック */
  /* 異常なサンプルあたりビット数 */
  if ((enc_param->bits_per_sample == 0)
      || (enc_param->bits_per_sample > AAD_MAX_BITS_PER_SAMPLE)) {
    return AAD_ERROR_INVALID_FORMAT;
  }
  /* ブロックサイズが小さすぎる */
  if (enc_param->max_block_size < AAD_BLOCK_HEADER_SIZE(enc_param->num_channels)) {
    return AAD_ERROR_INVALID_FORMAT;
  }
  /* 異常なマルチチャンネル処理法 */
  if (enc_param->ch_process_method >= AAD_CH_PROCESS_METHOD_INVALID) {
    return AAD_ERROR_INVALID_FORMAT;
  }

  /* 総サンプル数 */
  tmp_header.num_samples = num_samples;

  /* そのままヘッダに入れられるメンバ */
  tmp_header.num_channels = enc_param->num_channels;
  tmp_header.sampling_rate = enc_param->sampling_rate;
  tmp_header.bits_per_sample = enc_param->bits_per_sample;
  tmp_header.ch_process_method = enc_param->ch_process_method;

  /* ブロックサイズとブロックあたりサンプル数はAPIで計算 */
  if (AADEncoder_CalculateBlockSize(
        enc_param->max_block_size, enc_param->num_channels, enc_param->bits_per_sample,
        &tmp_header.block_size, &tmp_header.num_samples_per_block) != AAD_APIRESULT_OK) {
    return AAD_ERROR_INVALID_FORMAT;
  }

  /* 成功終了 */
  (*header_info) = tmp_header;

  return AAD_ERROR_OK;
}

/* エンコードパラメータの設定 */
AADApiResult AADEncoder_SetEncodeParameter(
    struct AADEncoder *encoder, const struct AADEncodeParameter *parameter)
{
  uint32_t ch;
  struct AADHeaderInfo tmp_header = { 0, };

  /* 引数チェック */
  if ((encoder == NULL) || (parameter == NULL)) {
    return AAD_APIRESULT_INVALID_ARGUMENT;
  }

  /* パラメータ設定がおかしくないか、ヘッダへの変換を通じて確認 */
  /* 総サンプル数はダミー値を入れる */
  if (AADEncoder_ConvertParameterToHeader(parameter, 0, &tmp_header) != AAD_ERROR_OK) {
    return AAD_APIRESULT_INVALID_FORMAT;
  }

  /* テーブルの初期化 */
  for (ch = 0; ch < AAD_MAX_NUM_CHANNELS; ch++) {
    AADTable_Initialize(&(encoder->processor[ch].table), parameter->bits_per_sample);
  }

  /* エンコード繰り返し回数のセット */
  encoder->num_encode_trials = parameter->num_encode_trials;

  /* ヘッダ設定 */
  encoder->header = tmp_header;

  /* パラメータ設定済みフラグを立てる */
  encoder->set_parameter = 1;

  return AAD_APIRESULT_OK;
}

/* ヘッダ含めファイル全体をエンコード */
AADApiResult AADEncoder_EncodeWhole(
    struct AADEncoder *encoder,
    const int32_t *const *input, uint32_t num_samples,
    uint8_t *data, uint32_t data_size, uint32_t *output_size)
{
  AADApiResult ret;
  uint32_t progress, ch, write_size, write_offset, num_encode_samples;
  uint8_t *data_pos;
  const int32_t *input_ptr[AAD_MAX_NUM_CHANNELS];
  const struct AADHeaderInfo *header;

  /* 引数チェック */
  if ((encoder == NULL) || (input == NULL)
      || (data == NULL) || (output_size == NULL)) {
    return AAD_APIRESULT_INVALID_ARGUMENT;
  }

  /* パラメータ未セットではエンコードできない */
  if (encoder->set_parameter == 0) {
    return AAD_APIRESULT_PARAMETER_NOT_SET;
  }

  /* 書き出し位置を取得 */
  data_pos = data;

  /* ヘッダエンコード */
  encoder->header.num_samples = num_samples;
  if ((ret = AADEncoder_EncodeHeader(&(encoder->header), data_pos, data_size))
      != AAD_APIRESULT_OK) {
    return ret;
  }
  header = &(encoder->header);

  /* 進捗状況初期化 */
  progress = 0;
  write_offset = AAD_HEADER_SIZE;
  data_pos = data + AAD_HEADER_SIZE;

  /* ブロックを時系列順にエンコード */
  while (progress < num_samples) {
    /* エンコードサンプル数の確定 */
    num_encode_samples
      = AAD_MIN_VAL(header->num_samples_per_block, num_samples - progress);
    /* サンプル参照位置のセット */
    for (ch = 0; ch < header->num_channels; ch++) {
      input_ptr[ch] = &input[ch][progress];
    }

    /* 性能のよいプロセッサの探索 */
    if (encoder->num_encode_trials > 0) {
      struct AADEncodeProcessor best_processor[AAD_MAX_NUM_CHANNELS];
      if (AADEncoder_SearchBestProcessor(
            encoder, input, progress, num_encode_samples, &best_processor[0]) != AAD_ERROR_OK) {
        return AAD_APIRESULT_NG;
      }
      /* 見つけたプロセッサをセット */
      memcpy(encoder->processor, &best_processor, sizeof(struct AADEncodeProcessor) * header->num_channels);
    }

    /* ブロックエンコード */
    if ((ret = AADEncoder_EncodeBlock(encoder,
            input_ptr, num_encode_samples,
            data_pos, data_size - write_offset, &write_size)) != AAD_APIRESULT_OK) {
      return ret;
    }

    /* 進捗更新 */
    data_pos      += write_size;
    write_offset  += write_size;
    progress      += num_encode_samples;
    AAD_ASSERT(write_size <= header->block_size);
    AAD_ASSERT(write_offset <= data_size);
  }

  /* 成功終了 */
  (*output_size) = write_offset;
  return AAD_APIRESULT_OK;
}
