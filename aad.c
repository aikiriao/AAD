#include "aad.h"
#include "byte_array.h"

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* TODO: for debug */
#include <math.h>

/* アラインメント */
#define AAD_ALIGNMENT                 16

#define AAD_FILTER_ORDER              4
#define AAD_FIXEDPOINT_DIGITS         15
#define AAD_FIXEDPOINT_0_5            (1 << (AAD_FIXEDPOINT_DIGITS - 1))
#define AAD_LMSFILTER_MU              ((1 << AAD_FIXEDPOINT_DIGITS) >> 4)
#define AAD_PREEMPHASIS_SHIFT         5
#define AAD_PREEMPHASIS_NUMER         ((1 << AAD_PREEMPHASIS_SHIFT) - 1)

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

/* 指定サンプル数が占めるデータサイズ[byte]を計算 */
#define AAD_CALCULATE_DATASIZE_BYTE(num_samples, bits_per_sample) \
  (AAD_ROUND_UP((num_samples) * (bits_per_sample), 8) / 8)

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
  int16_t history[AAD_FILTER_ORDER];
  int32_t weight[AAD_FILTER_ORDER];
  uint8_t stepsize_index;         /* ステップサイズテーブルの参照インデックス     */
  int32_t quantize_error;         /* 量子化誤差（エンコード時のみ） */
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
    struct AADProcessor *processor, uint8_t nibble);

/* 1サンプルエンコード */
static uint8_t AADProcessor_EncodeSample(
    struct AADProcessor *processor, int32_t sample);

/* 単一データブロックエンコード */
static AADApiResult AADEncoder_EncodeBlock(
    struct AADEncoder *encoder,
    const int32_t *const *input, uint32_t num_samples, 
    uint8_t *data, uint32_t data_size, uint32_t *output_size);

/* インデックス変動テーブル */
static const int8_t AAD_index_table_4bit[16] = {
  -1, -1, -1, -1, 2, 4, 6, 8, 
  -1, -1, -1, -1, 2, 4, 6, 8 
};

static const int8_t AAD_index_table_3bit[8] = {
  -1, -1, 2, 4,
  -1, -1, 2, 4,
};

static const int8_t AAD_index_table_2bit[4] = {
  -1, 2,
  -1, 2,
};

static const int8_t AAD_index_table_1bit[2] = {
  -1, 2,
};

/* ステップサイズ量子化テーブル */
#if 0
static const uint16_t AAD_stepsize_table[89] = {
      7,     8,     9,    10,    11,    12,    13,    14, 
     16,    17,    19,    21,    23,    25,    28,    31, 
     34,    37,    41,    45,    50,    55,    60,    66,
     73,    80,    88,    97,   107,   118,   130,   143, 
    157,   173,   190,   209,   230,   253,   279,   307,
    337,   371,   408,   449,   494,   544,   598,   658,
    724,   796,   876,   963,  1060,  1166,  1282,  1411, 
   1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,
   3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,
   7132,  7845,  8630,  9493, 10442, 11487, 12635, 13899,
  15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
  32767
};
#else
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
#endif

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
  interleave_data_unit_size = AAD_CalculateLCM(8, num_channels * bits_per_sample) / 8;
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
    struct AADProcessor *processor, uint8_t nibble)
{
  int16_t idx;
  int32_t sample, qdiff, delta, predict, stepsize;

  assert(processor != NULL);

  /* 頻繁に参照する変数をオート変数に受ける */
  idx = processor->stepsize_index;

  /* ステップサイズの取得 */
  stepsize = AAD_stepsize_table[idx];

  /* 差分算出 */
  /* diff = stepsize * (delta + 0.5) / 4 */
  /* -> diff = stepsize * (delta * 2 + 1) / 8 */
  delta = nibble & 7;
  qdiff = (stepsize * ((delta << 1) + 1)) >> 3;
  qdiff = (nibble & 8) ? -qdiff : qdiff; /* 符号ビットの反映 */
#if 0
  /* 3bit */
  /* diff = stepsize * (delta + 0.5) / 2 */
  /* -> diff = stepsize * (delta * 2 + 1) / 4 */
  delta = nibble & 3;
  qdiff = (stepsize * ((delta << 1) + 1)) >> 2;
  qdiff = (nibble & 4) ? -qdiff : qdiff;
  /* 2bit */
  /* diff = stepsize * (delta + 0.5) */
  /* -> diff = stepsize * (delta * 2 + 1) / 2 */
  delta = nibble & 1;
  qdiff = (stepsize * ((delta << 1) + 1)) >> 1;
  qdiff = (nibble & 2) ? -qdiff : qdiff;
#endif

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
  idx += AAD_index_table_4bit[nibble];
#if 0
  idx += AAD_index_table_3bit[nibble];
  idx += AAD_index_table_2bit[nibble];
#endif
  idx = AAD_INNER_VAL(idx, 0, (int16_t)((sizeof(AAD_stepsize_table) / sizeof(uint16_t)) - 1));

  /* 計算結果の反映 */
  assert(idx <= UINT8_MAX);
  processor->stepsize_index = (uint8_t)idx;

  /* 係数更新 */
  processor->weight[3] += (qdiff * processor->history[3] + AAD_FIXEDPOINT_0_5) >> (AAD_FIXEDPOINT_DIGITS + 3);
  processor->weight[2] += (qdiff * processor->history[2] + AAD_FIXEDPOINT_0_5) >> (AAD_FIXEDPOINT_DIGITS + 3);
  processor->weight[1] += (qdiff * processor->history[1] + AAD_FIXEDPOINT_0_5) >> (AAD_FIXEDPOINT_DIGITS + 3);
  processor->weight[0] += (qdiff * processor->history[0] + AAD_FIXEDPOINT_0_5) >> (AAD_FIXEDPOINT_DIGITS + 3);

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
  if (data_size <= AAD_BLOCK_HEADER_SIZE(header->num_channels)) {
    return AAD_APIRESULT_INSUFFICIENT_DATA;
  }

  /* 読み出しポインタのセット */
  read_pos = data;

  /* デコード可能なサンプル数を計算 */
  tmp_num_decode_samples
    = AAD_MIN_VAL(header->num_samples_per_block,
        AAD_NUM_SAMPLES_IN_BLOCK(data_size, header->num_channels, header->bits_per_sample));

  /* バッファサイズチェック */
  if ((buffer_num_channels < header->num_channels)
      || (buffer_num_samples < tmp_num_decode_samples)) {
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
        uint8_t code;
        for (ch = 0; ch < header->num_channels; ch++) {
          assert((uint32_t)(read_pos - data) < data_size);
          assert((uint32_t)(read_pos - data) < header->block_size);
          ByteArray_GetUint8(read_pos, &code);
          // printf("%d %d %d \n", smpl, (uint32_t)(read_pos - data), code);
          buffer[ch][smpl + 0] = AADProcessor_DecodeSample(&(decoder->processor[ch]), (code >> 4) & 0xF); 
          buffer[ch][smpl + 1] = AADProcessor_DecodeSample(&(decoder->processor[ch]), (code >> 0) & 0xF); 
          assert((uint32_t)(read_pos - data) <= data_size);
          assert((uint32_t)(read_pos - data) <= header->block_size);
          /* FIXME: smpl + 1 がバッファオーバーランする可能性がある */
        }
        /* TODO: MS -> LR */
      }
      break;
    case 2:
    case 3:
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
    struct AADProcessor *processor, int32_t sample)
{
  uint8_t nibble;
  int16_t idx;
  int32_t predict, diff, qdiff, delta, stepsize, diffabs, sign;
  int32_t quantize_sample;

  assert(processor != NULL);
  
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
  diff -= (processor->quantize_error >> 2); /* 量子化誤差をフィードバック（ノイズシェーピング） */
  sign = diff < 0;
  diffabs = sign ? -diff : diff;

  /* 差分を符号表現に変換 */
  /* nibble = sign(diff) * round(|diff| * 4 / stepsize) */
  nibble = (uint8_t)AAD_MIN_VAL((diffabs << 2) / stepsize, 7);
#if 0
  /* 3bit */
  nibble = (uint8_t)AAD_MIN_VAL((diffabs << 1) / stepsize, 3);
  /* 2bit */
  nibble = (uint8_t)AAD_MIN_VAL(diffabs / stepsize, 1);
#endif
  /* nibbleの最上位ビットは符号ビット */
  if (sign) {
    nibble |= 0x8;  /* 4bit */
#if 0
    nibble |= 0x4; /* 3bit */
    nibble |= 0x2; /* 2bit */
#endif
  }

  /* 量子化した差分を計算 */
  /* diff = stepsize * (delta + 0.5) / 4 */
  /* -> diff = stepsize * (delta * 2 + 1) / 8 */
  delta = nibble & 7;
  qdiff = (stepsize * ((delta << 1) + 1)) >> 3;
  qdiff = (nibble & 8) ? -qdiff : qdiff; /* 符号ビットの反映 */
#if 0
  /* 3bit */
  /* diff = stepsize * (delta + 0.5) / 2 */
  /* -> diff = stepsize * (delta * 2 + 1) / 4 */
  delta = nibble & 3;
  qdiff = (stepsize * ((delta << 1) + 1)) >> 2;
  qdiff = (nibble & 4) ? -qdiff : qdiff;
  /* 2bit */
  /* diff = stepsize * (delta + 0.5) */
  /* -> diff = stepsize * (delta * 2 + 1) / 2 */
  delta = nibble & 1;
  qdiff = (stepsize * ((delta << 1) + 1)) >> 1;
  qdiff = (nibble & 2) ? -qdiff : qdiff;
#endif

  /* インデックス更新 */
  idx += AAD_index_table_4bit[nibble];
#if 0
  idx += AAD_index_table_3bit[nibble];
  idx += AAD_index_table_2bit[nibble];
#endif
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
  processor->weight[3] += (qdiff * processor->history[3] + AAD_FIXEDPOINT_0_5) >> (AAD_FIXEDPOINT_DIGITS + 3);
  processor->weight[2] += (qdiff * processor->history[2] + AAD_FIXEDPOINT_0_5) >> (AAD_FIXEDPOINT_DIGITS + 3);
  processor->weight[1] += (qdiff * processor->history[1] + AAD_FIXEDPOINT_0_5) >> (AAD_FIXEDPOINT_DIGITS + 3);
  processor->weight[0] += (qdiff * processor->history[0] + AAD_FIXEDPOINT_0_5) >> (AAD_FIXEDPOINT_DIGITS + 3);

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

  /* これこわい */
  assert(num_samples >= AAD_FILTER_ORDER);

  /* フィルタに先頭サンプルをセット */
  for (ch = 0; ch < header->num_channels; ch++) {
    for (smpl = 0; smpl < AAD_FILTER_ORDER; smpl++) {
      assert(input[ch][smpl] <= INT16_MAX);
      assert(input[ch][smpl] >= INT16_MIN);
      encoder->processor[ch].history[AAD_FILTER_ORDER - smpl - 1]
        = (int16_t)input[ch][smpl];
    }
  }

  /* ブロックヘッダエンコード */
  for (ch = 0; ch < header->num_channels; ch++) {
    /* フィルタの状態 */
    for (smpl = 0; smpl < AAD_FILTER_ORDER; smpl++) {
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

  /* TODO: LR -> MSをどこに突っ込むよ？ここ？ */

  /* データエンコード */
  switch (header->bits_per_sample) {
    case 4:
      for (smpl = AAD_FILTER_ORDER; smpl < num_samples; smpl += 2) {
        uint8_t code[2];
        /* TODO: LR -> MSここかも 2x6のバッファを用意 */
        for (ch = 0; ch < header->num_channels; ch++) {
          assert((uint32_t)(data_pos - data) < data_size);
          assert((uint32_t)(data_pos - data) < header->block_size);
          /* FIXME: smpl + 1 がオーバーランする可能性がある */
          code[0] = AADProcessor_EncodeSample(&(encoder->processor[ch]), input[ch][smpl + 0]); 
          code[1] = AADProcessor_EncodeSample(&(encoder->processor[ch]), input[ch][smpl + 1]); 
          assert((code[0] <= AAD_MAX_CODE_VALUE) && (code[1] <= AAD_MAX_CODE_VALUE));
          ByteArray_PutUint8(data_pos, (code[0] << 4) | code[1]);
          // printf("%d %d %d \n", smpl, (uint32_t)(data_pos - data), (code[0] << 4) | code[1]);
          assert((uint32_t)(data_pos - data) <= data_size);
          assert((uint32_t)(data_pos - data) <= header->block_size);
        }
      }
      break;
    case 2:
    case 3:
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
  /* 異常なブロックサイズ */
  if (enc_param->max_block_size <= AAD_BLOCK_HEADER_SIZE(enc_param->num_channels)) {
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

  /* ブロックあたりサンプル数 */

  /* 成功終了 */
  (*header_info) = tmp_header;

  return AAD_ERROR_OK;
}

/* エンコードパラメータの設定 */
AADApiResult AADEncoder_SetEncodeParameter(
    struct AADEncoder *encoder, const struct AADEncodeParameter *parameter)
{
  struct AADHeaderInfo tmp_header = {0, };

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

  progress = 0;
  write_offset = AAD_HEADER_SIZE;
  data_pos = data + AAD_HEADER_SIZE;
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

