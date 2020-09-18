#include "aad_decoder.h"
#include "aad_internal.h"
#include "byte_array.h"

#include <stdlib.h>

/* デコード処理ハンドル */
struct AADDecodeProcessor {
  int16_t history[AAD_FILTER_ORDER];  /* 入力データ履歴 */
  int32_t weight[AAD_FILTER_ORDER];   /* フィルタ係数   */
  uint8_t stepsize_index;             /* ステップサイズテーブルの参照インデックス     */
};

/* デコーダハンドル */
struct AADDecoder {
  struct AADHeaderInfo      header;
  struct AADDecodeProcessor processor[AAD_MAX_NUM_CHANNELS];
  uint8_t                   alloced_by_own;
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

  /* デコード処理ハンドルのリセット */
  for (ch = 0; ch < AAD_MAX_NUM_CHANNELS; ch++) {
    AADDecodeProcessor_Reset(&(decoder->processor[ch]));
  }

  /* メモリ領域先頭の記録 */
  decoder->work = work;
  
  /* 自前確保であることをマーク */
  decoder->alloced_by_own = tmp_alloced_by_own;

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
  if ((u16buf == 0) || (u16buf > AAD_MAX_BITS_PER_SAMPLE)) {
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
  int32_t sample, qdiff, delta, predict, stepsize;
  const uint8_t signbit = (uint8_t)(1U << (bits_per_sample - 1));
  const uint8_t absmask = signbit - 1;

  AAD_ASSERT(processor != NULL);
  AAD_ASSERT((bits_per_sample >= 2) && (bits_per_sample <= AAD_MAX_BITS_PER_SAMPLE));
  AAD_ASSERT(code <= ((1U << bits_per_sample) - 1));

  /* 頻繁に参照する変数をオート変数に受ける */
  idx = processor->stepsize_index;

  /* ステップサイズの取得 */
  stepsize = AAD_stepsize_table[idx];

  /* 差分算出 */
  /* diff = stepsize * (delta + 0.5) / 2**(bits_per_sample-2) */
  /* -> diff = stepsize * (delta * 2 + 1) / 2**(bits_per_sample-1) */
  delta = code & absmask;
  qdiff = (stepsize * ((delta << 1) + 1)) >> (bits_per_sample - 1);
  qdiff = (code & signbit) ? -qdiff : qdiff; /* 符号ビットの反映 */

  /* フィルタ予測 */
  predict = AAD_FIXEDPOINT_0_5;
  predict += processor->history[0] * processor->weight[0];
  predict += processor->history[1] * processor->weight[1];
  predict += processor->history[2] * processor->weight[2];
  predict += processor->history[3] * processor->weight[3];
  predict >>= AAD_FIXEDPOINT_DIGITS;

  /* 予測を加え信号を復元 */
  sample = qdiff + predict;
  /* 16bit幅にクリップ */
  sample = AAD_INNER_VAL(sample, INT16_MIN, INT16_MAX);

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

  /* 係数更新 */
  processor->weight[3] += (qdiff * processor->history[3] + AAD_FIXEDPOINT_0_5) >> (AAD_FIXEDPOINT_DIGITS + AAD_LMSFILTER_SHIFT);
  processor->weight[2] += (qdiff * processor->history[2] + AAD_FIXEDPOINT_0_5) >> (AAD_FIXEDPOINT_DIGITS + AAD_LMSFILTER_SHIFT);
  processor->weight[1] += (qdiff * processor->history[1] + AAD_FIXEDPOINT_0_5) >> (AAD_FIXEDPOINT_DIGITS + AAD_LMSFILTER_SHIFT);
  processor->weight[0] += (qdiff * processor->history[0] + AAD_FIXEDPOINT_0_5) >> (AAD_FIXEDPOINT_DIGITS + AAD_LMSFILTER_SHIFT);

  /* 入力データ履歴更新 */
  processor->history[3] = processor->history[2];
  processor->history[2] = processor->history[1];
  processor->history[1] = processor->history[0];
  processor->history[0] = (int16_t)sample;

  return sample;
}

/* 単一データブロックデコード */
static AADApiResult AADDecoder_DecodeBlock(
    struct AADDecoder *decoder,
    const uint8_t *data, uint32_t data_size, 
    int32_t **buffer, uint32_t buffer_num_channels, uint32_t buffer_num_samples, 
    uint32_t *num_decode_samples)
{
  const struct AADHeaderInfo *header;
  uint32_t ch, smpl;
  const uint8_t *read_pos;
  uint8_t u8buf;
  uint32_t tmp_num_decode_samples;

  /* 引数チェック */
  if ((decoder == NULL) || (data == NULL)
      || (buffer == NULL) || (num_decode_samples == NULL)) {
    return AAD_APIRESULT_INVALID_ARGUMENT;
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
    /* フィルタの状態 */
    for (smpl = 0; smpl < AAD_FILTER_ORDER; smpl++) {
      ByteArray_GetUint16BE(read_pos, &u16buf);
      decoder->processor[ch].weight[smpl] = (int16_t)u16buf;
      ByteArray_GetUint16BE(read_pos, &u16buf);
      decoder->processor[ch].history[smpl] = (int16_t)u16buf;
    }
    /* ステップサイズインデックス */
    ByteArray_GetUint8(read_pos, &(decoder->processor[ch].stepsize_index));
    /* 予約領域 */
    ByteArray_GetUint8(read_pos, &u8buf);
    AAD_ASSERT(u8buf == 0);
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
        uint32_t  i;
        uint8_t   code;
        int32_t   outbuf[AAD_MAX_NUM_CHANNELS][2];
        for (ch = 0; ch < header->num_channels; ch++) {
          AAD_ASSERT((uint32_t)(read_pos - data) < data_size);
          AAD_ASSERT((uint32_t)(read_pos - data) < header->block_size);
          ByteArray_GetUint8(read_pos, &code);
          outbuf[ch][0] = AADDecodeProcessor_DecodeSample(&(decoder->processor[ch]), (code >> 4) & 0xF, 4); 
          outbuf[ch][1] = AADDecodeProcessor_DecodeSample(&(decoder->processor[ch]), (code >> 0) & 0xF, 4); 
          AAD_ASSERT((uint32_t)(read_pos - data) <= data_size);
          AAD_ASSERT((uint32_t)(read_pos - data) <= header->block_size);
        }
        for (ch = 0; ch < header->num_channels; ch++) {
          for (i = 0; (i < 2) && ((smpl + i) < tmp_num_decode_samples); i++) {
            AAD_ASSERT((smpl + i) < buffer_num_samples);
            buffer[ch][smpl + i] = outbuf[ch][i];
          }
        }
      }
      break;
    case 3:
      for (smpl = AAD_FILTER_ORDER; smpl < tmp_num_decode_samples; smpl += 8) {
        uint32_t  i;
        uint8_t   code[3];
        int32_t   outbuf[AAD_MAX_NUM_CHANNELS][8];
        for (ch = 0; ch < header->num_channels; ch++) {
          AAD_ASSERT((uint32_t)(read_pos - data) < data_size);
          AAD_ASSERT((uint32_t)(read_pos - data) < header->block_size);
          ByteArray_GetUint8(read_pos, &code[0]);
          ByteArray_GetUint8(read_pos, &code[1]);
          ByteArray_GetUint8(read_pos, &code[2]);
          /* TODO: 3bit単位の符号出し入れのマクロ化 */
          outbuf[ch][0] = AADDecodeProcessor_DecodeSample(&(decoder->processor[ch]), (code[0] >> 5) & 0x7, 3); 
          outbuf[ch][1] = AADDecodeProcessor_DecodeSample(&(decoder->processor[ch]), (code[0] >> 2) & 0x7, 3); 
          outbuf[ch][2] = AADDecodeProcessor_DecodeSample(&(decoder->processor[ch]), (uint8_t)(((code[0] & 0x3) << 1) | ((code[1] >> 7) & 0x1)), 3); 
          outbuf[ch][3] = AADDecodeProcessor_DecodeSample(&(decoder->processor[ch]), (code[1] >> 4) & 0x7, 3); 
          outbuf[ch][4] = AADDecodeProcessor_DecodeSample(&(decoder->processor[ch]), (code[1] >> 1) & 0x7, 3); 
          outbuf[ch][5] = AADDecodeProcessor_DecodeSample(&(decoder->processor[ch]), (uint8_t)(((code[1] & 0x1) << 2) | ((code[2] >> 6) & 0x3)), 3); 
          outbuf[ch][6] = AADDecodeProcessor_DecodeSample(&(decoder->processor[ch]), (code[2] >> 3) & 0x7, 3); 
          outbuf[ch][7] = AADDecodeProcessor_DecodeSample(&(decoder->processor[ch]), (code[2] >> 0) & 0x7, 3); 
          AAD_ASSERT((uint32_t)(read_pos - data) <= data_size);
          AAD_ASSERT((uint32_t)(read_pos - data) <= header->block_size);
        }
        for (ch = 0; ch < header->num_channels; ch++) {
          for (i = 0; (i < 8) && ((smpl + i) < tmp_num_decode_samples); i++) {
            AAD_ASSERT((smpl + i) < buffer_num_samples);
            buffer[ch][smpl + i] = outbuf[ch][i];
          }
        }
      }
      break;
    case 2:
      for (smpl = AAD_FILTER_ORDER; smpl < tmp_num_decode_samples; smpl += 4) {
        uint32_t  i;
        uint8_t   code;
        int32_t   outbuf[AAD_MAX_NUM_CHANNELS][4];
        for (ch = 0; ch < header->num_channels; ch++) {
          AAD_ASSERT((uint32_t)(read_pos - data) < data_size);
          AAD_ASSERT((uint32_t)(read_pos - data) < header->block_size);
          ByteArray_GetUint8(read_pos, &code);
          outbuf[ch][0] = AADDecodeProcessor_DecodeSample(&(decoder->processor[ch]), (code >> 6) & 0x3, 2); 
          outbuf[ch][1] = AADDecodeProcessor_DecodeSample(&(decoder->processor[ch]), (code >> 4) & 0x3, 2); 
          outbuf[ch][2] = AADDecodeProcessor_DecodeSample(&(decoder->processor[ch]), (code >> 2) & 0x3, 2); 
          outbuf[ch][3] = AADDecodeProcessor_DecodeSample(&(decoder->processor[ch]), (code >> 0) & 0x3, 2); 
          AAD_ASSERT((uint32_t)(read_pos - data) <= data_size);
          AAD_ASSERT((uint32_t)(read_pos - data) <= header->block_size);
        }
        for (ch = 0; ch < header->num_channels; ch++) {
          for (i = 0; (i < 4) && ((smpl + i) < tmp_num_decode_samples); i++) {
            AAD_ASSERT((smpl + i) < buffer_num_samples);
            buffer[ch][smpl + i] = outbuf[ch][i];
          }
        }
      }
      break;
    default:
      return AAD_APIRESULT_INVALID_FORMAT;
  }

  /* MS -> LR */
  if ((header->num_channels >= 2)
      && (header->ch_process_method == AAD_CH_PROCESS_METHOD_MS)) {
    int32_t mid, side;
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

  /* ヘッダデコード */
  if ((ret = AADDecoder_DecodeHeader(data, data_size, &(decoder->header)))
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
