#include "aad.h"
#include "byte_array.h"

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* アラインメント */
#define AAD_ALIGNMENT                 16

/* エンコード/デコード時に使用する定数 */
#define AAD_FILTER_ORDER              4                                   /* フィルタ係数長     */
#define AAD_FIXEDPOINT_DIGITS         15                                  /* 固定小数点の小数桁 */
#define AAD_FIXEDPOINT_0_5            (1 << (AAD_FIXEDPOINT_DIGITS - 1))  /* 固定小数点の0.5    */
#define AAD_NOISE_SHAPING_SHIFT       2                                   /* ノイズシェーピングのフィードバックゲインの右シフト量 */
#define AAD_LMSFILTER_SHIFT           4                                   /* LMSフィルタ係数更新時のシフト量 */

/* 最大の符号化値 */
#define AAD_MAX_CODE_VALUE            ((1 << AAD_MAX_BITS_PER_SAMPLE) - 1)

/* nの倍数への切り上げ */
#define AAD_ROUND_UP(val, n)          ((((val) + ((n) - 1)) / (n)) * (n))

/* 最大値を選択 */
#define AAD_MAX_VAL(a, b)             (((a) > (b)) ? (a) : (b))

/* 最小値を選択 */
#define AAD_MIN_VAL(a, b)             (((a) < (b)) ? (a) : (b))

/* min以上max未満に制限 */
#define AAD_INNER_VAL(val, min, max)  AAD_MAX_VAL(min, AAD_MIN_VAL(max, val))

/* ブロックヘッダサイズの計算 */
#define AAD_BLOCK_HEADER_SIZE(num_channels) (18 * (num_channels))

/* 指定データサイズ内に含まれるサンプル数を計算 */
#define AAD_NUM_SAMPLES_IN_DATA(data_size, num_channels, bits_per_sample) \
  ((data_size) * 8) / ((num_channels) * (bits_per_sample))

/* ブロック内に含まれるサンプル数を計算 */
#define AAD_NUM_SAMPLES_IN_BLOCK(data_size, num_channels, bits_per_sample) \
  (AAD_FILTER_ORDER + AAD_NUM_SAMPLES_IN_DATA((data_size) - AAD_BLOCK_HEADER_SIZE(num_channels), (num_channels), (bits_per_sample)))

/* 内部エラー型 */
typedef enum AADErrorTag {
  AAD_ERROR_OK = 0,              /* OK */
  AAD_ERROR_NG,                  /* 分類不能な失敗 */
  AAD_ERROR_INVALID_ARGUMENT,    /* 不正な引数 */
  AAD_ERROR_INVALID_FORMAT,      /* 不正なフォーマット       */
  AAD_ERROR_INSUFFICIENT_BUFFER, /* バッファサイズが足りない */
  AAD_ERROR_INSUFFICIENT_DATA    /* データサイズが足りない   */
} AADError;

/* コア処理ハンドル */
struct AADProcessor {
  int16_t history[AAD_FILTER_ORDER];  /* 入力データ履歴 */
  int32_t weight[AAD_FILTER_ORDER];   /* フィルタ係数   */
  uint8_t stepsize_index;             /* ステップサイズテーブルの参照インデックス     */
  int32_t quantize_error;             /* 量子化誤差（FIXME: エンコード時のみ） */
};

/* デコーダ */
struct AADDecoder {
  struct AADHeaderInfo  header;
  struct AADProcessor   processor[AAD_MAX_NUM_CHANNELS];
  uint8_t               alloced_by_own;
  void                  *work;
};

/* エンコーダ */
struct AADEncoder {
  struct AADHeaderInfo  header;
  uint8_t               set_parameter;
  struct AADProcessor   processor[AAD_MAX_NUM_CHANNELS];
  uint8_t               alloced_by_own;
  void                  *work;
};

/* 1サンプルデコード */
static int32_t AADProcessor_DecodeSample(
    struct AADProcessor *processor, uint8_t nibble, uint8_t bpsample);

/* 1サンプルエンコード */
static uint8_t AADProcessor_EncodeSample(
    struct AADProcessor *processor, int32_t sample, uint8_t bpsample);

/* 単一データブロックエンコード */
static AADApiResult AADEncoder_EncodeBlock(
    struct AADEncoder *encoder,
    const int32_t *const *input, uint32_t num_samples, 
    uint8_t *data, uint32_t data_size, uint32_t *output_size);

/* インデックス変動テーブル: 4bit */
static const int8_t AAD_index_table_4bit[16] = {
  -1, -1, -1, -1, 2, 4, 6, 8, 
  -1, -1, -1, -1, 2, 4, 6, 8 
};

/* インデックス変動テーブル: 3bit */
static const int8_t AAD_index_table_3bit[8] = {
  -1, -1, 2, 4,
  -1, -1, 2, 4,
};

/* インデックス変動テーブル: 2bit */
static const int8_t AAD_index_table_2bit[4] = {
  -1, 2,
  -1, 2,
};

#if 0
/* インデックス変動テーブル: 1bit */
/* Future work... */
static const int8_t AAD_index_table_1bit[2] = {
  -1, 2,
};
#endif

/* ステップサイズ量子化テーブル */
/* x + 2 ** (log2(32767 - 255) / 255 * x) で生成 */
static const uint16_t AAD_stepsize_table[256] = {
  1, 2, 3, 4, 5, 6, 7, 8,
  9, 10, 12, 13, 14, 15, 16, 17,
  18, 19, 20, 21, 22, 23, 24, 26,
  27, 28, 29, 30, 31, 32, 33, 35,
  36, 37, 38, 39, 40, 42, 43, 44,
  45, 46, 48, 49, 50, 51, 53, 54,
  55, 56, 58, 59, 60, 62, 63, 64,
  66, 67, 69, 70, 72, 73, 75, 76,
  78, 79, 81, 82, 84, 86, 87, 89,
  91, 93, 94, 96, 98, 100, 102, 104,
  106, 108, 110, 112, 115, 117, 119, 122,
  124, 127, 129, 132, 134, 137, 140, 143,
  146, 149, 152, 155, 159, 162, 166, 169,
  173, 177, 181, 185, 189, 194, 198, 203,
  208, 213, 218, 223, 229, 235, 240, 247,
  253, 259, 266, 273, 280, 288, 296, 304,
  312, 321, 330, 339, 349, 359, 369, 380,
  391, 403, 415, 427, 440, 454, 468, 482,
  497, 513, 529, 546, 564, 582, 601, 621,
  641, 663, 685, 708, 732, 757, 783, 810,
  838, 867, 897, 929, 962, 996, 1031, 1068,
  1107, 1147, 1189, 1232, 1277, 1324, 1373, 1424,
  1477, 1532, 1589, 1649, 1711, 1776, 1843, 1913,
  1986, 2062, 2141, 2223, 2309, 2398, 2491, 2588,
  2688, 2793, 2902, 3016, 3134, 3257, 3386, 3519,
  3658, 3803, 3954, 4111, 4274, 4445, 4622, 4807,
  4999, 5199, 5408, 5625, 5851, 6086, 6332, 6587,
  6853, 7130, 7418, 7719, 8032, 8358, 8697, 9050,
  9418, 9802, 10201, 10617, 11050, 11501, 11970, 12460,
  12969, 13500, 14053, 14628, 15228, 15852, 16503, 17180,
  17885, 18620, 19385, 20182, 21013, 21877, 22778, 23716,
  24693, 25710, 26770, 27874, 29023, 30221, 31468, 32767,
};

/* 最大公約数の計算 */
static uint32_t AAD_CalculateGCD(uint32_t a, uint32_t b)
{
  assert((a != 0) && (b != 0));
  if (a % b == 0) {
    return b;
  }
  return AAD_CalculateGCD(b, a % b);
}

/* 最小公倍数の計算 */
static uint32_t AAD_CalculateLCM(uint32_t a, uint32_t b)
{
  assert((a != 0) && (b != 0));
  return (a * b) / AAD_CalculateGCD(a, b);
}

/* ブロックサイズとブロックあたりサンプル数の計算 */
AADApiResult AADEncoder_CalculateBlockSize(
    uint32_t max_block_size, uint16_t num_channels, uint32_t bits_per_sample,
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
  interleave_data_unit_size = num_channels * (AAD_CalculateLCM(8, bits_per_sample) / 8);
  num_samples_per_interleave_data_unit = (interleave_data_unit_size * 8) / (num_channels * bits_per_sample);

  /* ブロックデータサイズの計算 */
  block_data_size = max_block_size - AAD_BLOCK_HEADER_SIZE(num_channels);
  /* データ単位サイズに切り捨て */
  block_data_size = interleave_data_unit_size * (block_data_size / interleave_data_unit_size);

  /* ブロックデータ内に入れられるサンプル数を計算 */
  num_samples_in_block_data
    = num_samples_per_interleave_data_unit * (block_data_size / interleave_data_unit_size);
  
  /* ブロックサイズの確定 */
  assert(AAD_BLOCK_HEADER_SIZE(num_channels) + block_data_size <= UINT16_MAX);
  (*block_size) = (uint16_t)(AAD_BLOCK_HEADER_SIZE(num_channels) + block_data_size);
  /* ヘッダに入っている分を加算する */
  if (num_samples_per_block != NULL) {
    (*num_samples_per_block) = num_samples_in_block_data + AAD_FILTER_ORDER;
  }
  return AAD_APIRESULT_OK;
}

/* ワークサイズ計算 */
int32_t AADDecoder_CalculateWorkSize(void)
{
  return AAD_ALIGNMENT + sizeof(struct AADDecoder);
}

/* デコードハンドル作成 */
struct AADDecoder *AADDecoder_Create(void *work, int32_t work_size)
{
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

  /* ハンドルの中身を0初期化 */
  memset(decoder, 0, sizeof(struct AADDecoder));

  /* メモリ領域先頭の記録 */
  decoder->work = work;
  
  /* 自前確保であることをマーク */
  decoder->alloced_by_own = tmp_alloced_by_own;

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
      fprintf(stderr, "Invalid signature: \"%c%c%c%c\" \n", buf[0], buf[1], buf[2], buf[3]);
      return AAD_APIRESULT_INVALID_FORMAT;
    }
  }

  /* フォーマットバージョン */
  ByteArray_GetUint32BE(data_pos, &u32buf);
  if (u32buf != AAD_FORMAT_VERSION) {
    fprintf(stderr, "Unsupported format version: %d \n", u32buf);
    return AAD_APIRESULT_INVALID_FORMAT;
  }

  /* チャンネル数 */
  ByteArray_GetUint16BE(data_pos, &u16buf);
  if ((u16buf == 0) || (u16buf > AAD_MAX_NUM_CHANNELS)) {
    fprintf(stderr, "Unsupported channels: %d \n", u16buf);
    return AAD_APIRESULT_INVALID_FORMAT;
  }
  tmp_header_info.num_channels = u16buf;
  /* サンプル数 */
  ByteArray_GetUint32BE(data_pos, &u32buf);
  if (u32buf == 0) {
    fprintf(stderr, "Invalid number of samples: %d \n", u32buf);
    return AAD_APIRESULT_INVALID_FORMAT;
  }
  tmp_header_info.num_samples = u32buf;
  /* サンプリングレート */
  ByteArray_GetUint32BE(data_pos, &u32buf);
  if (u32buf == 0) {
    fprintf(stderr, "Invalid sampling_rate: %d \n", u32buf);
    return AAD_APIRESULT_INVALID_FORMAT;
  }
  tmp_header_info.sampling_rate = u32buf;
  /* サンプルあたりビット数 */
  ByteArray_GetUint16BE(data_pos, &u16buf);
  if ((u16buf == 0) || (u16buf > AAD_MAX_BITS_PER_SAMPLE)) {
    fprintf(stderr, "Unsupported bits per sample: %d \n", u16buf);
    return AAD_APIRESULT_INVALID_FORMAT;
  }
  tmp_header_info.bits_per_sample = u16buf;
  /* ブロックサイズ */
  ByteArray_GetUint16BE(data_pos, &u16buf);
  if (u16buf <= AAD_BLOCK_HEADER_SIZE(tmp_header_info.num_channels)) {
    fprintf(stderr, "Invalid block size: %d \n", u16buf);
    return AAD_APIRESULT_INVALID_FORMAT;
  }
  tmp_header_info.block_size = u16buf;
  /* ブロックあたりサンプル数 */
  ByteArray_GetUint32BE(data_pos, &u32buf);
  if (u32buf == 0) {
    fprintf(stderr, "Invalid num samples per block: %d \n", u32buf);
    return AAD_APIRESULT_INVALID_FORMAT;
  }
  tmp_header_info.num_samples_per_block = u32buf;

  /* ヘッダサイズチェック */
  assert((data_pos - data) == AAD_HEADER_SIZE);

  /* 成功終了 */
  (*header_info) = tmp_header_info;
  return AAD_APIRESULT_OK;
}

/* 1サンプルデコード */
static int32_t AADProcessor_DecodeSample(
    struct AADProcessor *processor, uint8_t nibble, uint8_t bpsample)
{
  int16_t idx;
  int32_t sample, qdiff, delta, predict, stepsize;
  const uint8_t signbit = (uint8_t)(1U << (bpsample - 1));
  const uint8_t absmask = signbit - 1;

  assert(processor != NULL);
  assert((bpsample >= 2) && (bpsample <= AAD_MAX_BITS_PER_SAMPLE));
  assert(nibble <= ((1U << bpsample) - 1));

  /* 頻繁に参照する変数をオート変数に受ける */
  idx = processor->stepsize_index;

  /* ステップサイズの取得 */
  stepsize = AAD_stepsize_table[idx];

  /* 差分算出 */
  /* diff = stepsize * (delta + 0.5) / 2**(bpsample-2) */
  /* -> diff = stepsize * (delta * 2 + 1) / 2**(bpsample-1) */
  delta = nibble & absmask;
  qdiff = (stepsize * ((delta << 1) + 1)) >> (bpsample - 1);
  qdiff = (nibble & signbit) ? -qdiff : qdiff; /* 符号ビットの反映 */

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
  switch (bpsample) {
    case 4: idx += AAD_index_table_4bit[nibble]; break;
    case 3: idx += AAD_index_table_3bit[nibble]; break;
    case 2: idx += AAD_index_table_2bit[nibble]; break;
    default: assert(0);
  }
  idx = AAD_INNER_VAL(idx, 0, (int16_t)((sizeof(AAD_stepsize_table) / sizeof(uint16_t)) - 1));

  /* 計算結果の反映 */
  assert(idx <= UINT8_MAX);
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
    assert(u8buf == 0);
  }

  /* ブロックヘッダサイズチェック */
  assert((uint32_t)(read_pos - data) == AAD_BLOCK_HEADER_SIZE(header->num_channels));

  /* 先頭サンプルはヘッダに入っている */
  for (ch = 0; ch < header->num_channels; ch++) {
    for (smpl = 0; smpl < AAD_FILTER_ORDER; smpl++) {
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
          assert((uint32_t)(read_pos - data) < data_size);
          assert((uint32_t)(read_pos - data) < header->block_size);
          ByteArray_GetUint8(read_pos, &code);
          outbuf[ch][0] = AADProcessor_DecodeSample(&(decoder->processor[ch]), (code >> 4) & 0xF, 4); 
          outbuf[ch][1] = AADProcessor_DecodeSample(&(decoder->processor[ch]), (code >> 0) & 0xF, 4); 
          assert((uint32_t)(read_pos - data) <= data_size);
          assert((uint32_t)(read_pos - data) <= header->block_size);
        }
        /* TODO: MS -> LR */
        for (ch = 0; ch < header->num_channels; ch++) {
          for (i = 0; (i < 2) && ((smpl + i) < tmp_num_decode_samples); i++) {
            buffer[ch][smpl + i] = outbuf[ch][i];
            assert((smpl + i) < buffer_num_samples);
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
          assert((uint32_t)(read_pos - data) < data_size);
          assert((uint32_t)(read_pos - data) < header->block_size);
          ByteArray_GetUint8(read_pos, &code[0]);
          ByteArray_GetUint8(read_pos, &code[1]);
          ByteArray_GetUint8(read_pos, &code[2]);
          /* TODO: 3bit単位の符号出し入れのマクロ化 */
          outbuf[ch][0] = AADProcessor_DecodeSample(&(decoder->processor[ch]), (code[0] >> 5) & 0x7, 3); 
          outbuf[ch][1] = AADProcessor_DecodeSample(&(decoder->processor[ch]), (code[0] >> 2) & 0x7, 3); 
          outbuf[ch][2] = AADProcessor_DecodeSample(&(decoder->processor[ch]), (uint8_t)(((code[0] & 0x3) << 1) | ((code[1] >> 7) & 0x1)), 3); 
          outbuf[ch][3] = AADProcessor_DecodeSample(&(decoder->processor[ch]), (code[1] >> 4) & 0x7, 3); 
          outbuf[ch][4] = AADProcessor_DecodeSample(&(decoder->processor[ch]), (code[1] >> 1) & 0x7, 3); 
          outbuf[ch][5] = AADProcessor_DecodeSample(&(decoder->processor[ch]), (uint8_t)(((code[1] & 0x1) << 2) | ((code[2] >> 6) & 0x3)), 3); 
          outbuf[ch][6] = AADProcessor_DecodeSample(&(decoder->processor[ch]), (code[2] >> 3) & 0x7, 3); 
          outbuf[ch][7] = AADProcessor_DecodeSample(&(decoder->processor[ch]), (code[2] >> 0) & 0x7, 3); 
          assert((uint32_t)(read_pos - data) <= data_size);
          assert((uint32_t)(read_pos - data) <= header->block_size);
        }
        /* TODO: MS -> LR */
        for (ch = 0; ch < header->num_channels; ch++) {
          for (i = 0; (i < 8) && ((smpl + i) < tmp_num_decode_samples); i++) {
            buffer[ch][smpl + i] = outbuf[ch][i];
            assert((smpl + i) < buffer_num_samples);
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
          assert((uint32_t)(read_pos - data) < data_size);
          assert((uint32_t)(read_pos - data) < header->block_size);
          ByteArray_GetUint8(read_pos, &code);
          outbuf[ch][0] = AADProcessor_DecodeSample(&(decoder->processor[ch]), (code >> 6) & 0x3, 2); 
          outbuf[ch][1] = AADProcessor_DecodeSample(&(decoder->processor[ch]), (code >> 4) & 0x3, 2); 
          outbuf[ch][2] = AADProcessor_DecodeSample(&(decoder->processor[ch]), (code >> 2) & 0x3, 2); 
          outbuf[ch][3] = AADProcessor_DecodeSample(&(decoder->processor[ch]), (code >> 0) & 0x3, 2); 
          assert((uint32_t)(read_pos - data) <= data_size);
          assert((uint32_t)(read_pos - data) <= header->block_size);
        }
        /* TODO: MS -> LR */
        for (ch = 0; ch < header->num_channels; ch++) {
          for (i = 0; (i < 4) && ((smpl + i) < tmp_num_decode_samples); i++) {
            buffer[ch][smpl + i] = outbuf[ch][i];
            assert((smpl + i) < buffer_num_samples);
          }
        }
      }
      break;
    default:
      return AAD_APIRESULT_INVALID_FORMAT;
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
    assert(progress <= buffer_num_samples);
    assert(read_offset <= data_size);
  }

  /* 成功終了 */
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

  /* ヘッダサイズチェック */
  assert((data_pos - data) == AAD_HEADER_SIZE);

  /* 成功終了 */
  return AAD_APIRESULT_OK;
}

/* エンコーダワークサイズ計算 */
int32_t AADEncoder_CalculateWorkSize(void)
{
  return AAD_ALIGNMENT + sizeof(struct AADEncoder);
}

/* エンコーダハンドル作成 */
struct AADEncoder *AADEncoder_Create(void *work, int32_t work_size)
{
  struct AADEncoder *encoder;
  uint8_t *work_ptr;
  uint8_t tmp_alloced_by_own = 0;

  /* 領域自前確保の場合 */
  if ((work == NULL) && (work_size == 0)) {
    work_size = AADEncoder_CalculateWorkSize();
    work = malloc((uint32_t)work_size);
    tmp_alloced_by_own = 1;
  }

  /* 引数チェック */
  if ((work == NULL) || (work_size < AADEncoder_CalculateWorkSize())) {
    return NULL;
  }

  work_ptr = (uint8_t *)work;

  /* アラインメントを揃えてから構造体を配置 */
  work_ptr = (uint8_t *)AAD_ROUND_UP((uintptr_t)work_ptr, AAD_ALIGNMENT);
  encoder = (struct AADEncoder *)work_ptr;

  /* ハンドルの中身を0初期化 */
  memset(encoder, 0, sizeof(struct AADEncoder));

  /* パラメータは未セット状態に */
  encoder->set_parameter = 0;

  /* メモリ先頭アドレスを記録 */
  encoder->work = work;

  /* 自前確保であることをマーク */
  encoder->alloced_by_own = tmp_alloced_by_own;

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

/* 1サンプルエンコード */
static uint8_t AADProcessor_EncodeSample(
    struct AADProcessor *processor, int32_t sample, uint8_t bpsample)
{
  uint8_t nibble;
  int16_t idx;
  int32_t predict, diff, qdiff, delta, stepsize, diffabs, sign;
  int32_t quantize_sample;
  const uint8_t signbit = (uint8_t)(1U << (bpsample - 1));
  const uint8_t absmask = signbit - 1;

  assert(processor != NULL);
  assert((bpsample >= 2) && (bpsample <= AAD_MAX_BITS_PER_SAMPLE));
  
  /* 頻繁に参照する変数をオート変数に受ける */
  idx = processor->stepsize_index;

  /* ステップサイズの取得 */
  stepsize = AAD_stepsize_table[idx];

  /* フィルタ予測 */
  predict = AAD_FIXEDPOINT_0_5;
  predict += processor->history[0] * processor->weight[0];
  predict += processor->history[1] * processor->weight[1];
  predict += processor->history[2] * processor->weight[2];
  predict += processor->history[3] * processor->weight[3];
  predict >>= AAD_FIXEDPOINT_DIGITS;

  /* 差分 */
  diff = sample - predict;
  diff -= (processor->quantize_error >> AAD_NOISE_SHAPING_SHIFT); /* 量子化誤差をフィードバック（ノイズシェーピング） */
  sign = diff < 0;
  diffabs = sign ? -diff : diff;

  /* 差分を符号表現に変換 */
  /* nibble = sign(diff) * round(|diff| * 2**(bpsample-2) / stepsize) */
  nibble = (uint8_t)AAD_MIN_VAL((diffabs << (bpsample - 2)) / stepsize, absmask);
  /* nibbleの最上位ビットは符号ビット */
  if (sign) {
    nibble |= signbit;
  }

  /* 量子化した差分を計算 */
  /* diff = stepsize * (delta + 0.5) / 2**(bpsample-2) */
  /* -> diff = stepsize * (delta * 2 + 1) / 2**(bpsample-1) */
  delta = nibble & absmask;
  qdiff = (stepsize * ((delta << 1) + 1)) >> (bpsample - 1);
  qdiff = (sign) ? -qdiff : qdiff; /* 符号ビットの反映 */

  /* インデックス更新 */
  switch (bpsample) {
    case 4: idx += AAD_index_table_4bit[nibble]; break;
    case 3: idx += AAD_index_table_3bit[nibble]; break;
    case 2: idx += AAD_index_table_2bit[nibble]; break;
    default: assert(0);
  }
  idx = AAD_INNER_VAL(idx, 0, (int16_t)((sizeof(AAD_stepsize_table) / sizeof(uint16_t)) - 1));

  /* 計算結果の反映 */
  assert(idx <= UINT8_MAX);
  processor->stepsize_index = (uint8_t)idx;

  /* 量子化後のサンプル値 */
  quantize_sample = qdiff + predict;
  /* 16bit幅にクリップ */
  quantize_sample = AAD_INNER_VAL(quantize_sample, INT16_MIN, INT16_MAX);
  /* 誤差の量子化誤差 */
  processor->quantize_error = diff - qdiff;

  /* フィルタ係数更新 */
  processor->weight[3] += (qdiff * processor->history[3] + AAD_FIXEDPOINT_0_5) >> (AAD_FIXEDPOINT_DIGITS + AAD_LMSFILTER_SHIFT);
  processor->weight[2] += (qdiff * processor->history[2] + AAD_FIXEDPOINT_0_5) >> (AAD_FIXEDPOINT_DIGITS + AAD_LMSFILTER_SHIFT);
  processor->weight[1] += (qdiff * processor->history[1] + AAD_FIXEDPOINT_0_5) >> (AAD_FIXEDPOINT_DIGITS + AAD_LMSFILTER_SHIFT);
  processor->weight[0] += (qdiff * processor->history[0] + AAD_FIXEDPOINT_0_5) >> (AAD_FIXEDPOINT_DIGITS + AAD_LMSFILTER_SHIFT);

  /* 入力データ履歴更新 */
  processor->history[3] = processor->history[2];
  processor->history[2] = processor->history[1];
  processor->history[1] = processor->history[0];
  processor->history[0] = (int16_t)quantize_sample;

  assert(nibble <= AAD_MAX_CODE_VALUE);
  return nibble;
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

  /* 引数チェック */
  if ((encoder == NULL) || (data == NULL)
      || (input == NULL) || (output_size == NULL)) {
    return AAD_APIRESULT_INVALID_ARGUMENT;
  }
  header = &(encoder->header);

  /* 書き出しポインタのセット */
  data_pos = data;

  /* フィルタに先頭サンプルをセット */
  for (ch = 0; ch < header->num_channels; ch++) {
    /* 総サンプル数がフィルタ次数より少ない場合がある */
    uint32_t num_buffer = AAD_MIN_VAL(AAD_FILTER_ORDER, num_samples);
    for (smpl = 0; smpl < AAD_FILTER_ORDER; smpl++) {
      encoder->processor[ch].history[AAD_FILTER_ORDER - smpl - 1] = 0;
      if (smpl < num_buffer) {
        assert(input[ch][smpl] <= INT16_MAX); assert(input[ch][smpl] >= INT16_MIN);
        encoder->processor[ch].history[AAD_FILTER_ORDER - smpl - 1]
          = (int16_t)input[ch][smpl];
      }
    }
  }

  /* ブロックヘッダエンコード */
  for (ch = 0; ch < header->num_channels; ch++) {
    /* フィルタの状態 */
    for (smpl = 0; smpl < AAD_FILTER_ORDER; smpl++) {
      encoder->processor[ch].weight[smpl]
        = AAD_INNER_VAL(encoder->processor[ch].weight[smpl], INT16_MIN, INT16_MAX);
      assert(encoder->processor[ch].weight[smpl] <= INT16_MAX);
      assert(encoder->processor[ch].weight[smpl] >= INT16_MIN);
      ByteArray_PutUint16BE(data_pos, (uint16_t)encoder->processor[ch].weight[smpl]);
      ByteArray_PutUint16BE(data_pos, encoder->processor[ch].history[smpl]);
    }
    /* ステップサイズインデックス */
    ByteArray_PutUint8(data_pos, encoder->processor[ch].stepsize_index);
    /* 予約領域 */
    ByteArray_PutUint8(data_pos, 0);
  }

  /* ブロックヘッダサイズチェック */
  assert((uint32_t)(data_pos - data) == AAD_BLOCK_HEADER_SIZE(header->num_channels));

  /* データエンコード */
  switch (header->bits_per_sample) {
    case 4:
      for (smpl = AAD_FILTER_ORDER; smpl < num_samples; smpl += 2) {
        uint8_t code[2];
        int32_t inbuf[AAD_MAX_NUM_CHANNELS][2] = { { 0, } };
        /* TODO: LR -> MS */
        for (ch = 0; ch < header->num_channels; ch++) {
          uint32_t i;
          for (i = 0; (i < 2) && ((smpl + i) < num_samples); i++) {
            inbuf[ch][i] = input[ch][smpl + i];
          }
        }
        for (ch = 0; ch < header->num_channels; ch++) {
          assert((uint32_t)(data_pos - data) < data_size);
          assert((uint32_t)(data_pos - data) < header->block_size);
          code[0] = AADProcessor_EncodeSample(&(encoder->processor[ch]), input[ch][smpl + 0], 4);
          code[1] = AADProcessor_EncodeSample(&(encoder->processor[ch]), input[ch][smpl + 1], 4);
          assert((code[0] <= 0xF) && (code[1] <= 0xF));
          ByteArray_PutUint8(data_pos, (code[0] << 4) | code[1]);
          assert((uint32_t)(data_pos - data) <= data_size);
          assert((uint32_t)(data_pos - data) <= header->block_size);
        }
      }
      break;
    case 3:
      for (smpl = AAD_FILTER_ORDER; smpl < num_samples; smpl += 8) {
        uint8_t code[8];
        uint8_t outbuf[3];
        int32_t inbuf[AAD_MAX_NUM_CHANNELS][8] = { { 0, } };
        /* TODO: LR -> MS */
        for (ch = 0; ch < header->num_channels; ch++) {
          uint32_t i;
          for (i = 0; (i < 8) && ((smpl + i) < num_samples); i++) {
            inbuf[ch][i] = input[ch][smpl + i];
          }
        }
        for (ch = 0; ch < header->num_channels; ch++) {
          assert((uint32_t)(data_pos - data) < data_size);
          assert((uint32_t)(data_pos - data) < header->block_size);
          code[0] = AADProcessor_EncodeSample(&(encoder->processor[ch]), inbuf[ch][0], 3);
          code[1] = AADProcessor_EncodeSample(&(encoder->processor[ch]), inbuf[ch][1], 3);
          code[2] = AADProcessor_EncodeSample(&(encoder->processor[ch]), inbuf[ch][2], 3);
          code[3] = AADProcessor_EncodeSample(&(encoder->processor[ch]), inbuf[ch][3], 3);
          code[4] = AADProcessor_EncodeSample(&(encoder->processor[ch]), inbuf[ch][4], 3);
          code[5] = AADProcessor_EncodeSample(&(encoder->processor[ch]), inbuf[ch][5], 3);
          code[6] = AADProcessor_EncodeSample(&(encoder->processor[ch]), inbuf[ch][6], 3);
          code[7] = AADProcessor_EncodeSample(&(encoder->processor[ch]), inbuf[ch][7], 3);
          assert((code[0] <= 0x7) && (code[1] <= 0x7) && (code[2] <= 0x7) && (code[3] <= 0x7)
              && (code[4] <= 0x7) && (code[5] <= 0x7) && (code[6] <= 0x7) && (code[7] <= 0x7));
          /* 3byteに詰める */
          outbuf[0] = (uint8_t)((code[0] << 5) | (code[1] << 2) | ((code[2] & 0x6) >> 1));
          outbuf[1] = (uint8_t)(((code[2] & 0x1) << 7) | (code[3] << 4) | (code[4] << 1) | ((code[5] & 0x4) >> 2));
          outbuf[2] = (uint8_t)(((code[5] & 0x3) << 6) | (code[6] << 3) | (code[7]));
          assert((outbuf[0] <= 0xFF) && (outbuf[1] <= 0xFF) && (outbuf[2] <= 0xFF));
          ByteArray_PutUint8(data_pos, outbuf[0]);
          ByteArray_PutUint8(data_pos, outbuf[1]);
          ByteArray_PutUint8(data_pos, outbuf[2]);
          assert((uint32_t)(data_pos - data) <= data_size);
          assert((uint32_t)(data_pos - data) <= header->block_size);
        }
      }
      break;
    case 2:
      for (smpl = AAD_FILTER_ORDER; smpl < num_samples; smpl += 4) {
        uint8_t code[4];
        int32_t inbuf[AAD_MAX_NUM_CHANNELS][4] = { { 0, } };
        /* TODO: LR -> MS */
        for (ch = 0; ch < header->num_channels; ch++) {
          uint32_t i;
          for (i = 0; (i < 4) && ((smpl + i) < num_samples); i++) {
            inbuf[ch][i] = input[ch][smpl + i];
          }
        }
        for (ch = 0; ch < header->num_channels; ch++) {
          assert((uint32_t)(data_pos - data) < data_size);
          assert((uint32_t)(data_pos - data) < header->block_size);
          code[0] = AADProcessor_EncodeSample(&(encoder->processor[ch]), inbuf[ch][0], 2);
          code[1] = AADProcessor_EncodeSample(&(encoder->processor[ch]), inbuf[ch][1], 2);
          code[2] = AADProcessor_EncodeSample(&(encoder->processor[ch]), inbuf[ch][2], 2);
          code[3] = AADProcessor_EncodeSample(&(encoder->processor[ch]), inbuf[ch][3], 2);
          assert((code[0] <= 0x3) && (code[1] <= 0x3) && (code[2] <= 0x3) && (code[3] <= 0x3));
          ByteArray_PutUint8(data_pos, (code[0] << 6) | (code[1] << 4) | (code[2] << 2) | ((code[3] << 0)));
          assert((uint32_t)(data_pos - data) <= data_size);
          assert((uint32_t)(data_pos - data) <= header->block_size);
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

  /* 総サンプル数 */
  tmp_header.num_samples = num_samples;

  /* そのままヘッダに入れられるメンバ */
  tmp_header.num_channels = enc_param->num_channels;
  tmp_header.sampling_rate = enc_param->sampling_rate;
  tmp_header.bits_per_sample = enc_param->bits_per_sample;

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

  /* フィルタの適応を早めるため、先頭部分を一回空エンコード */
  if ((ret = AADEncoder_EncodeBlock(encoder,
          input, AAD_MIN_VAL(header->num_samples_per_block, num_samples),
          data_pos, data_size - write_offset, &write_size)) != AAD_APIRESULT_OK) {
    return ret;
  }

  /* ブロックを時系列順にエンコード */
  while (progress < num_samples) {
    /* エンコードサンプル数の確定 */
    num_encode_samples
      = AAD_MIN_VAL(header->num_samples_per_block, num_samples - progress);
    /* サンプル参照位置のセット */
    for (ch = 0; ch < header->num_channels; ch++) {
      input_ptr[ch] = &input[ch][progress];
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
    assert(write_size <= header->block_size);
    assert(write_offset <= data_size);
  }

  /* 成功終了 */
  (*output_size) = write_offset;
  return AAD_APIRESULT_OK;
}

