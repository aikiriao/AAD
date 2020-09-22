#include "aad_encoder.h"
#include "aad_internal.h"
#include "byte_array.h"

#include <stdlib.h>
#include <string.h>

/* エンコード処理ハンドル */
struct AADEncodeProcessor {
  int16_t history[AAD_FILTER_ORDER];  /* 入力データ履歴 */
  int32_t weight[AAD_FILTER_ORDER];   /* フィルタ係数   */
  uint8_t stepsize_index;             /* ステップサイズテーブルの参照インデックス */
};

/* エンコーダ */
struct AADEncoder {
  struct AADHeaderInfo      header;
  uint8_t                   set_parameter;
  struct AADEncodeProcessor processor[AAD_MAX_NUM_CHANNELS];
  uint8_t                   alloced_by_own;
  uint8_t                   num_encode_trials;
  int32_t                   *input_buffer[AAD_MAX_NUM_CHANNELS];
  void                      *work;
};

/* エンコード処理ハンドルのリセット */
static void AADEncodeProcessor_Reset(struct AADEncodeProcessor *processor);

/* 1サンプルエンコード */
static uint8_t AADEncodeProcessor_EncodeSample(
    struct AADEncodeProcessor *processor, int32_t sample, uint8_t bits_per_sample);

/* 単一データブロックエンコード */
static AADApiResult AADEncoder_EncodeBlock(
    struct AADEncoder *encoder,
    const int32_t *const *input, uint32_t num_samples, 
    uint8_t *data, uint32_t data_size, uint32_t *output_size);

/* テーブル */
#include "aad_tables.c"

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
  AAD_ASSERT(AAD_BLOCK_HEADER_SIZE(num_channels) + block_data_size <= UINT16_MAX);
  (*block_size) = (uint16_t)(AAD_BLOCK_HEADER_SIZE(num_channels) + block_data_size);
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
  switch (header_info->bits_per_sample) {
    case 2: case 3: case 4: break;
    default: return AAD_APIRESULT_INVALID_FORMAT;
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
  /* フォーマットバージョン */
  ByteArray_PutUint32BE(data_pos, AAD_FORMAT_VERSION);
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
  if (AADEncoder_CalculateBlockSize(
        max_block_size, 1, AAD_MIN_BITS_PER_SAMPLE,
        &block_size, &num_samples_per_block) != AAD_APIRESULT_OK) {
    return -1;
  }

  /* 構造体サイズ */
  work_size = AAD_ALIGNMENT + sizeof(struct AADEncoder);

  /* バッファサイズ */
  work_size += AAD_MAX_NUM_CHANNELS * (int32_t)(sizeof(int32_t) * num_samples_per_block + AAD_ALIGNMENT);

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

  processor->stepsize_index = 0;
}

/* 1サンプルエンコード */
static uint8_t AADEncodeProcessor_EncodeSample(
    struct AADEncodeProcessor *processor, int32_t sample, uint8_t bits_per_sample)
{
  uint8_t code;
  int16_t idx;
  int32_t predict, diff, qdiff, delta, stepsize, diffabs, sign;
  int32_t quantize_sample, ord;
  const uint8_t signbit = (uint8_t)(1U << (bits_per_sample - 1));
  const uint8_t absmask = signbit - 1;

  AAD_ASSERT(processor != NULL);
  AAD_ASSERT((bits_per_sample >= AAD_MIN_BITS_PER_SAMPLE) && (bits_per_sample <= AAD_MAX_BITS_PER_SAMPLE));
  
  /* 頻繁に参照する変数をオート変数に受ける */
  idx = processor->stepsize_index;

  /* ステップサイズの取得 */
  stepsize = AAD_stepsize_table[idx];

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
  switch (bits_per_sample) {
    case 4:
      AAD_ASSERT(code < AAD_NUM_TABLE_ELEMENTS(AAD_index_table_4bit));
      idx += AAD_index_table_4bit[code];
      break;
    case 3:
      AAD_ASSERT(code < AAD_NUM_TABLE_ELEMENTS(AAD_index_table_3bit)); 
      idx += AAD_index_table_3bit[code];
      break;
    case 2:
      AAD_ASSERT(code < AAD_NUM_TABLE_ELEMENTS(AAD_index_table_2bit));
      idx += AAD_index_table_2bit[code];
      break;
    default: AAD_ASSERT(0);
  }
  idx = AAD_INNER_VAL(idx, 0, (int16_t)(AAD_NUM_TABLE_ELEMENTS(AAD_stepsize_table) - 1));

  /* 計算結果の反映 */
  AAD_ASSERT(idx <= UINT8_MAX);
  processor->stepsize_index = (uint8_t)idx;

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
  if ((header->num_channels >= 2)
      && (header->ch_process_method == AAD_CH_PROCESS_METHOD_MS)) {
    int32_t mid, side;
    /* 音が割れて誤差が増大するのを防ぐため、変換時に右シフト */
    for (smpl = 0; smpl < num_samples; smpl++) {
      mid  = (buffer[0][smpl] + buffer[1][smpl]) >> 1;
      side = (buffer[0][smpl] - buffer[1][smpl]) >> 1;
      buffer[0][smpl] = AAD_INNER_VAL(mid,  INT16_MIN, INT16_MAX);
      buffer[1][smpl] = AAD_INNER_VAL(side, INT16_MIN, INT16_MAX);
    }
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
    /* フィルタの状態 */
    for (smpl = 0; smpl < AAD_FILTER_ORDER; smpl++) {
      encoder->processor[ch].weight[smpl]
        = AAD_INNER_VAL(encoder->processor[ch].weight[smpl], INT16_MIN, INT16_MAX);
      AAD_ASSERT(encoder->processor[ch].weight[smpl] <= INT16_MAX);
      AAD_ASSERT(encoder->processor[ch].weight[smpl] >= INT16_MIN);
      ByteArray_PutUint16BE(data_pos, (uint16_t)encoder->processor[ch].weight[smpl]);
      ByteArray_PutUint16BE(data_pos, encoder->processor[ch].history[smpl]);
    }
    /* ステップサイズインデックス */
    ByteArray_PutUint8(data_pos, encoder->processor[ch].stepsize_index);
    /* 予約領域 */
    ByteArray_PutUint8(data_pos, 0);
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
        uint8_t outbuf[3];
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
          outbuf[0] = (uint8_t)((code[0] << 5) | (code[1] << 2) | ((code[2] & 0x6) >> 1));
          outbuf[1] = (uint8_t)(((code[2] & 0x1) << 7) | (code[3] << 4) | (code[4] << 1) | ((code[5] & 0x4) >> 2));
          outbuf[2] = (uint8_t)(((code[5] & 0x3) << 6) | (code[6] << 3) | (code[7]));
          AAD_ASSERT((outbuf[0] <= 0xFF) && (outbuf[1] <= 0xFF) && (outbuf[2] <= 0xFF));
          ByteArray_PutUint8(data_pos, outbuf[0]);
          ByteArray_PutUint8(data_pos, outbuf[1]);
          ByteArray_PutUint8(data_pos, outbuf[2]);
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
  struct AADHeaderInfo tmp_header = {0, };

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

  /* エンコード繰り返し回数のセット */
  if (parameter->num_encode_trials == 0) {
    return AAD_APIRESULT_INVALID_FORMAT;
  }
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
  uint32_t trial;
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
    /* 適応を早めるために連続するブロックを複数回エンコード */
    for (trial = 0; trial < encoder->num_encode_trials; trial++) {
      /* 前のブロック */
      if (progress >= header->num_samples_per_block) {
        const int32_t *prev_input_ptr[AAD_MAX_NUM_CHANNELS];
        for (ch = 0; ch < header->num_channels; ch++) {
          prev_input_ptr[ch] = &input[ch][progress - header->num_samples_per_block];
        }
        if ((ret = AADEncoder_EncodeBlock(encoder,
                prev_input_ptr, header->num_samples_per_block,
                data_pos, data_size - write_offset, &write_size)) != AAD_APIRESULT_OK) {
          return ret;
        }
      }
      /* エンコード対象のブロック */
      if ((ret = AADEncoder_EncodeBlock(encoder,
              input_ptr, num_encode_samples,
              data_pos, data_size - write_offset, &write_size)) != AAD_APIRESULT_OK) {
        return ret;
      }
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
