#include "aad_decoder.h"
#include "aad_internal.h"
#include "byte_array.h"

#include <stdlib.h>
#include <string.h>

/* デコード処理ハンドル */
struct AADDecodeProcessor {
  int16_t history[AAD_FILTER_ORDER];  /* 入力データ履歴 */
  int32_t weight[AAD_FILTER_ORDER];   /* フィルタ係数   */
  int16_t stepsize_index;             /* ステップサイズテーブルの参照インデックス     */
};

/* デコーダハンドル */
struct AADDecoder {
  struct AADHeaderInfo      header;
  struct AADDecodeProcessor processor[AAD_MAX_NUM_CHANNELS];
  uint8_t                   alloced_by_own;
  uint8_t                   set_header;
  void                      *work;
};

/* デコード処理ハンドルのリセット */
static void AADDecodeProcessor_Reset(struct AADDecodeProcessor *processor);

/* 1サンプルデコード */
static int32_t AADDecodeProcessor_DecodeSample(
    struct AADDecodeProcessor *processor, uint8_t code, uint8_t bits_per_sample);

/* テーブル */
#include "aad_tables.c"

/* ワークサイズ計算 */
int32_t AADDecoder_CalculateWorkSize(void)
{
  return AAD_ALIGNMENT + sizeof(struct AADDecoder);
}

/* デコードハンドル作成 */
struct AADDecoder *AADDecoder_Create(void *work, int32_t work_size)
{
  uint32_t ch;
  struct AADDecoder *decoder;
  uint8_t *work_ptr;
  uint8_t tmp_alloced_by_own = 0;

  /* 領域自前確保の場合 */
  if ((work == NULL) && (work_size == 0)) {
    work_size = AADDecoder_CalculateWorkSize();
    work = malloc((uint32_t)work_size);
    tmp_alloced_by_own = 1;
  }

  /* 引数チェック */
  if ((work == NULL) || (work_size < AADDecoder_CalculateWorkSize())) {
    return NULL;
  }

  work_ptr = (uint8_t *)work;

  /* アラインメントを揃えてから構造体を配置 */
  work_ptr = (uint8_t *)AAD_ROUND_UP((uintptr_t)work_ptr, AAD_ALIGNMENT);
  decoder = (struct AADDecoder *)work_ptr;
  work_ptr += sizeof(struct AADDecoder);

  /* デコード処理ハンドルのリセット */
  for (ch = 0; ch < AAD_MAX_NUM_CHANNELS; ch++) {
    AADDecodeProcessor_Reset(&(decoder->processor[ch]));
  }

  /* メモリ領域先頭の記録 */
  decoder->work = work;
  
  /* 自前確保であることをマーク */
  decoder->alloced_by_own = tmp_alloced_by_own;

  /* ヘッダは未セット状態 */
  decoder->set_header = 0;

  /* バッファオーバーランチェック */
  AAD_ASSERT((int32_t)(work_ptr - (uint8_t *)work) <= work_size);

  return decoder;
}

/* デコードハンドル破棄 */
void AADDecoder_Destroy(struct AADDecoder *decoder)
{
  if (decoder != NULL) {
    /* 自分で領域確保していたら破棄 */
    if (decoder->alloced_by_own == 1) {
      free(decoder->work);
    }
  }
}

/* ヘッダデコード */
AADApiResult AADDecoder_DecodeHeader(
    const uint8_t *data, uint32_t data_size, struct AADHeaderInfo *header_info)
{
  const uint8_t *data_pos;
  uint32_t u32buf;
  uint16_t u16buf;
  uint8_t  u8buf;
  struct AADHeaderInfo tmp_header_info;

  /* 引数チェック */
  if ((data == NULL) || (header_info == NULL)) {
    return AAD_APIRESULT_INVALID_ARGUMENT;
  }

  /* データサイズが足りない */
  if (data_size < AAD_HEADER_SIZE) {
    return AAD_APIRESULT_INSUFFICIENT_DATA;
  }

  /* 読み出し用ポインタ設定 */
  data_pos = data;

  /* シグネチャ */
  {
    uint8_t buf[4];
    ByteArray_GetUint8(data_pos, &buf[0]);
    ByteArray_GetUint8(data_pos, &buf[1]);
    ByteArray_GetUint8(data_pos, &buf[2]);
    ByteArray_GetUint8(data_pos, &buf[3]);
    if ((buf[0] != 'A') || (buf[1] != 'A')
        || (buf[2] != 'D') || (buf[3] != '\0')) {
      return AAD_APIRESULT_INVALID_FORMAT;
    }
  }

  /* フォーマットバージョン */
  ByteArray_GetUint32BE(data_pos, &u32buf);
  if (u32buf != AAD_FORMAT_VERSION) {
    return AAD_APIRESULT_INVALID_FORMAT;
  }

  /* コーデックバージョン */
  ByteArray_GetUint32BE(data_pos, &u32buf);
  if (u32buf != AAD_CODEC_VERSION) {
    /* バージョン不一致の場合は無条件で失敗 */
    return AAD_APIRESULT_INVALID_FORMAT;
  }

  /* チャンネル数 */
  ByteArray_GetUint16BE(data_pos, &u16buf);
  if ((u16buf == 0) || (u16buf > AAD_MAX_NUM_CHANNELS)) {
    return AAD_APIRESULT_INVALID_FORMAT;
  }
  tmp_header_info.num_channels = u16buf;
  /* サンプル数 */
  ByteArray_GetUint32BE(data_pos, &u32buf);
  if (u32buf == 0) {
    return AAD_APIRESULT_INVALID_FORMAT;
  }
  tmp_header_info.num_samples = u32buf;
  /* サンプリングレート */
  ByteArray_GetUint32BE(data_pos, &u32buf);
  if (u32buf == 0) {
    return AAD_APIRESULT_INVALID_FORMAT;
  }
  tmp_header_info.sampling_rate = u32buf;
  /* サンプルあたりビット数 */
  ByteArray_GetUint16BE(data_pos, &u16buf);
  if ((u16buf < AAD_MIN_BITS_PER_SAMPLE) || (u16buf > AAD_MAX_BITS_PER_SAMPLE)) {
    return AAD_APIRESULT_INVALID_FORMAT;
  }
  tmp_header_info.bits_per_sample = u16buf;
  /* ブロックサイズ */
  ByteArray_GetUint16BE(data_pos, &u16buf);
  if (u16buf <= AAD_BLOCK_HEADER_SIZE(tmp_header_info.num_channels)) {
    return AAD_APIRESULT_INVALID_FORMAT;
  }
  tmp_header_info.block_size = u16buf;
  /* ブロックあたりサンプル数 */
  ByteArray_GetUint32BE(data_pos, &u32buf);
  if (u32buf == 0) {
    return AAD_APIRESULT_INVALID_FORMAT;
  }
  tmp_header_info.num_samples_per_block = u32buf;
  /* マルチチャンネル処理法 */
  ByteArray_GetUint8(data_pos, &u8buf);
  if (u8buf >= AAD_CH_PROCESS_METHOD_INVALID) {
    return AAD_APIRESULT_INVALID_FORMAT;
  }
  tmp_header_info.ch_process_method = u8buf;
  /* モノラルではMS処理はできない */
  if ((tmp_header_info.ch_process_method == AAD_CH_PROCESS_METHOD_MS) 
      && (tmp_header_info.num_channels == 1)) {
    return AAD_APIRESULT_INVALID_FORMAT;
  }

  /* ヘッダサイズチェック */
  AAD_ASSERT((data_pos - data) == AAD_HEADER_SIZE);

  /* 成功終了 */
  (*header_info) = tmp_header_info;
  return AAD_APIRESULT_OK;
}

/* ヘッダのデコードとデコーダへのセット */
AADApiResult AADDecoder_DecodeAndSetHeader(
    struct AADDecoder *decoder, const uint8_t *data, uint32_t data_size)
{
  AADApiResult ret;
  struct AADHeaderInfo tmp_header;

  /* 引数チェック */
  if ((decoder == NULL) || (data == NULL)) {
    return AAD_APIRESULT_INVALID_ARGUMENT;
  }

  /* ヘッダデコード */
  if ((ret = AADDecoder_DecodeHeader(data, data_size, &tmp_header))
      != AAD_APIRESULT_OK) {
    return ret;
  }

  /* ヘッダセット */
  decoder->header = tmp_header;
  decoder->set_header = 1;

  return AAD_APIRESULT_OK;
}

/* デコード処理ハンドルのリセット */
static void AADDecodeProcessor_Reset(struct AADDecodeProcessor *processor)
{
  uint32_t i;

  AAD_ASSERT(processor != NULL);

  for (i = 0; i < AAD_FILTER_ORDER; i++) {
    processor->history[i] = 0;
    processor->weight[i] = 0;
  }

  processor->stepsize_index = 0;
}

/* 1サンプルデコード */
static int32_t AADDecodeProcessor_DecodeSample(
    struct AADDecodeProcessor *processor, uint8_t code, uint8_t bits_per_sample)
{
  int16_t idx;
  int32_t sample, qdiff, delta, predict, stepsize, ord;
  const uint8_t signbit = (uint8_t)(1U << (bits_per_sample - 1));
  const uint8_t absmask = (uint8_t)(signbit - 1);

  AAD_ASSERT(processor != NULL);
  AAD_ASSERT((bits_per_sample >= AAD_MIN_BITS_PER_SAMPLE) && (bits_per_sample <= AAD_MAX_BITS_PER_SAMPLE));
  AAD_ASSERT(code <= ((1U << bits_per_sample) - 1));

  /* 頻繁に参照する変数をオート変数に受ける */
  idx = processor->stepsize_index;

  /* ステップサイズの取得 */
  stepsize = AAD_TABLES_GET_STEPSIZE(idx);

  /* 差分算出 */
  /* diff = stepsize * (delta + 0.5) / 2**(bits_per_sample-2) */
  /* -> diff = stepsize * (delta * 2 + 1) / 2**(bits_per_sample-1) */
  delta = code & absmask;
  qdiff = (stepsize * ((delta << 1) + 1)) >> (bits_per_sample - 1);
  qdiff = (code & signbit) ? -qdiff : qdiff; /* 符号ビットの反映 */

  /* フィルタ予測 */
  predict = AAD_FIXEDPOINT_0_5;
  for (ord = 0; ord < AAD_FILTER_ORDER; ord++) {
    predict += processor->history[ord] * processor->weight[ord];
  }
  predict >>= AAD_FIXEDPOINT_DIGITS;

  /* 予測を加え信号を復元 */
  sample = qdiff + predict;
  /* 16bit幅にクリップ */
  sample = AAD_INNER_VAL(sample, INT16_MIN, INT16_MAX);

  /* インデックス更新 */
  AAD_TABLES_UPDATE_INDEX(idx, code, bits_per_sample);

  /* 計算結果の反映 */
  processor->stepsize_index = idx;

  /* 係数更新 */
  for (ord = 0; ord < AAD_FILTER_ORDER; ord++) {
    processor->weight[ord]
      += (qdiff * processor->history[ord] + AAD_FIXEDPOINT_0_5) >> (AAD_FIXEDPOINT_DIGITS + AAD_LMSFILTER_SHIFT);
  }

  /* 入力データ履歴更新 */
  for (ord = AAD_FILTER_ORDER - 1; ord > 0; ord--) {
    processor->history[ord] = processor->history[ord - 1];
  }
  processor->history[0] = (int16_t)sample;

  return sample;
}

/* 単一データブロックデコード */
AADApiResult AADDecoder_DecodeBlock(
    struct AADDecoder *decoder,
    const uint8_t *data, uint32_t data_size, 
    int32_t **buffer, uint32_t buffer_num_channels, uint32_t buffer_num_samples, 
    uint32_t *num_decode_samples)
{
  const struct AADHeaderInfo *header;
  uint32_t ch, smpl;
  const uint8_t *read_pos;
  uint32_t tmp_num_decode_samples;

  /* 引数チェック */
  if ((decoder == NULL) || (data == NULL)
      || (buffer == NULL) || (num_decode_samples == NULL)) {
    return AAD_APIRESULT_INVALID_ARGUMENT;
  }

  /* ヘッダがまだセットされていない */
  if (decoder->set_header != 1) {
    return AAD_APIRESULT_PARAMETER_NOT_SET;
  }

  /* ヘッダ取得 */
  header = &(decoder->header);

  /* ブロックヘッダのサイズに満たない */
  if (data_size < AAD_BLOCK_HEADER_SIZE(header->num_channels)) {
    return AAD_APIRESULT_INSUFFICIENT_DATA;
  }

  /* 読み出しポインタのセット */
  read_pos = data;

  /* デコードサンプル数を計算 */
  /* 補足: ブロック未満の場合はバッファがいっぱいになるまでデコード実行 */
  tmp_num_decode_samples = AAD_MIN_VAL(header->num_samples_per_block, buffer_num_samples);

  /* バッファサイズチェック */
  if (buffer_num_channels < header->num_channels) {
    return AAD_APIRESULT_INSUFFICIENT_BUFFER;
  }

  /* ブロックヘッダデコード */
  for (ch = 0; ch < header->num_channels; ch++) {
    uint16_t u16buf;
    uint8_t shift;
    /* ステップサイズインデックス12bit + 係数シフト量4bit */
    AAD_STATIC_ASSERT(AAD_TABLES_FLOAT_DIGITS == 4);
    ByteArray_GetUint16BE(read_pos, &u16buf);
    decoder->processor[ch].stepsize_index = (int16_t)(u16buf >> AAD_TABLES_FLOAT_DIGITS);
    shift = u16buf & 0xF;
    /* フィルタの状態 */
    for (smpl = 0; smpl < AAD_FILTER_ORDER; smpl++) {
      ByteArray_GetUint16BE(read_pos, &u16buf);
      decoder->processor[ch].weight[smpl] = (int16_t)u16buf;
      decoder->processor[ch].weight[smpl] <<= shift;
      ByteArray_GetUint16BE(read_pos, &u16buf);
      decoder->processor[ch].history[smpl] = (int16_t)u16buf;
    }
  }

  /* ブロックヘッダサイズチェック */
  AAD_ASSERT((uint32_t)(read_pos - data) == AAD_BLOCK_HEADER_SIZE(header->num_channels));

  /* 先頭サンプルはヘッダに入っている */
  for (ch = 0; ch < header->num_channels; ch++) {
    /* 最終ブロックがヘッダのみで終わっている場合があるため、バッファサイズを超えないようにする */
    for (smpl = 0; smpl < AAD_MIN_VAL(AAD_FILTER_ORDER, buffer_num_samples); smpl++) {
      buffer[ch][smpl] = decoder->processor[ch].history[AAD_FILTER_ORDER - smpl - 1];
    }
  }

  /* データデコード */
  switch (header->bits_per_sample) {
    case 4:
      for (smpl = AAD_FILTER_ORDER; smpl < tmp_num_decode_samples; smpl += 2) {
        const size_t copy_size = sizeof(int32_t) * AAD_MIN_VAL(2, tmp_num_decode_samples - smpl);
        for (ch = 0; ch < header->num_channels; ch++) {
          uint8_t code;
          int32_t outbuf[2];
          struct AADDecodeProcessor *processor = &(decoder->processor[ch]);
          AAD_ASSERT((uint32_t)(read_pos - data) < data_size);
          AAD_ASSERT((uint32_t)(read_pos - data) < header->block_size);
          ByteArray_GetUint8(read_pos, &code);
          outbuf[0] = AADDecodeProcessor_DecodeSample(processor, (code >> 4) & 0xF, 4); 
          outbuf[1] = AADDecodeProcessor_DecodeSample(processor, (code >> 0) & 0xF, 4); 
          memcpy(&buffer[ch][smpl], outbuf, copy_size);
        }
      }
      break;
    case 3:
      for (smpl = AAD_FILTER_ORDER; smpl < tmp_num_decode_samples; smpl += 8) {
        const size_t copy_size = sizeof(int32_t) * AAD_MIN_VAL(8, tmp_num_decode_samples - smpl);
        for (ch = 0; ch < header->num_channels; ch++) {
          uint32_t code24;
          int32_t outbuf[8];
          struct AADDecodeProcessor *processor = &(decoder->processor[ch]);
          AAD_ASSERT((uint32_t)(read_pos - data) < data_size);
          AAD_ASSERT((uint32_t)(read_pos - data) < header->block_size);
          ByteArray_GetUint24BE(read_pos, &code24);
          AAD_ASSERT((uint32_t)(read_pos - data) <= data_size);
          AAD_ASSERT((uint32_t)(read_pos - data) <= header->block_size);
          outbuf[0] = AADDecodeProcessor_DecodeSample(processor, (code24 >> 21) & 0x7, 3); 
          outbuf[1] = AADDecodeProcessor_DecodeSample(processor, (code24 >> 18) & 0x7, 3); 
          outbuf[2] = AADDecodeProcessor_DecodeSample(processor, (code24 >> 15) & 0x7, 3); 
          outbuf[3] = AADDecodeProcessor_DecodeSample(processor, (code24 >> 12) & 0x7, 3); 
          outbuf[4] = AADDecodeProcessor_DecodeSample(processor, (code24 >>  9) & 0x7, 3); 
          outbuf[5] = AADDecodeProcessor_DecodeSample(processor, (code24 >>  6) & 0x7, 3); 
          outbuf[6] = AADDecodeProcessor_DecodeSample(processor, (code24 >>  3) & 0x7, 3); 
          outbuf[7] = AADDecodeProcessor_DecodeSample(processor, (code24 >>  0) & 0x7, 3); 
          memcpy(&buffer[ch][smpl], outbuf, copy_size);
        }
      }
      break;
    case 2:
      for (smpl = AAD_FILTER_ORDER; smpl < tmp_num_decode_samples; smpl += 4) {
        const size_t copy_size = sizeof(int32_t) * AAD_MIN_VAL(4, tmp_num_decode_samples - smpl);
        for (ch = 0; ch < header->num_channels; ch++) {
          uint8_t code;
          int32_t outbuf[4];
          struct AADDecodeProcessor *processor = &(decoder->processor[ch]);
          AAD_ASSERT((uint32_t)(read_pos - data) < data_size);
          AAD_ASSERT((uint32_t)(read_pos - data) < header->block_size);
          ByteArray_GetUint8(read_pos, &code);
          outbuf[0] = AADDecodeProcessor_DecodeSample(processor, (code >> 6) & 0x3, 2); 
          outbuf[1] = AADDecodeProcessor_DecodeSample(processor, (code >> 4) & 0x3, 2); 
          outbuf[2] = AADDecodeProcessor_DecodeSample(processor, (code >> 2) & 0x3, 2); 
          outbuf[3] = AADDecodeProcessor_DecodeSample(processor, (code >> 0) & 0x3, 2); 
          memcpy(&buffer[ch][smpl], outbuf, copy_size);
        }
      }
      break;
    default:
      return AAD_APIRESULT_INVALID_FORMAT;
  }

  /* MS -> LR */
  if (header->ch_process_method == AAD_CH_PROCESS_METHOD_MS) {
    int32_t mid, side;
    /* チャンネル数チェック */
    if (header->num_channels < 2) {
      return AAD_APIRESULT_INVALID_FORMAT;
    }
    for (smpl = 0; smpl < tmp_num_decode_samples; smpl++) {
      mid   = buffer[0][smpl];
      side  = buffer[1][smpl];
      buffer[0][smpl] = AAD_INNER_VAL(mid + side, INT16_MIN, INT16_MAX);
      buffer[1][smpl] = AAD_INNER_VAL(mid - side, INT16_MIN, INT16_MAX);
    }
  }

  /* 成功終了 */
  (*num_decode_samples) = tmp_num_decode_samples;
  return AAD_APIRESULT_OK;
}

/* ヘッダ含めファイル全体をデコード */
AADApiResult AADDecoder_DecodeWhole(
    struct AADDecoder *decoder, const uint8_t *data, uint32_t data_size,
    int32_t **buffer, uint32_t buffer_num_channels, uint32_t buffer_num_samples)
{
  AADApiResult ret;
  uint32_t progress, ch, read_offset, read_block_size, num_decode_samples;
  const uint8_t *read_pos;
  int32_t *buffer_ptr[AAD_MAX_NUM_CHANNELS];
  const struct AADHeaderInfo *header;

  /* 引数チェック */
  if ((decoder == NULL) || (data == NULL) || (buffer == NULL)) {
    return AAD_APIRESULT_INVALID_ARGUMENT;
  }

  /* ヘッダデコードとデコーダへのセット */
  if ((ret = AADDecoder_DecodeAndSetHeader(decoder, data, data_size))
      != AAD_APIRESULT_OK) {
    return ret;
  }
  header = &(decoder->header);

  /* バッファサイズチェック */
  if ((buffer_num_channels < header->num_channels)
      || (buffer_num_samples < header->num_samples)) {
    return AAD_APIRESULT_INSUFFICIENT_BUFFER;
  }

  progress = 0;
  read_offset = AAD_HEADER_SIZE;
  read_pos = data + AAD_HEADER_SIZE;
  while ((progress < header->num_samples) && (read_offset < data_size)) {
    /* 読み出しサイズの確定 */
    read_block_size = AAD_MIN_VAL(data_size - read_offset, header->block_size);
    /* サンプル書き出し位置のセット */
    for (ch = 0; ch < header->num_channels; ch++) {
      buffer_ptr[ch] = &buffer[ch][progress];
    }
    /* ブロックデコード */
    if ((ret = AADDecoder_DecodeBlock(decoder,
          read_pos, read_block_size,
          buffer_ptr, buffer_num_channels, buffer_num_samples - progress, 
          &num_decode_samples)) != AAD_APIRESULT_OK) {
      return ret;
    }
    /* 進捗更新 */
    read_pos    += read_block_size;
    read_offset += read_block_size;
    progress    += num_decode_samples;
    AAD_ASSERT(progress <= buffer_num_samples);
    AAD_ASSERT(read_offset <= data_size);
  }

  /* 成功終了 */
  return AAD_APIRESULT_OK;
}
