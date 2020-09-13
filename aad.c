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

/* エンコード時に書き出すヘッダサイズ（データブロック直前までのファイルサイズ） */
#define AADENCODER_HEADER_SIZE        60

#define AAD_FIXEDPOINT_DIGITS         15
#define AAD_FIXEDPOINT_0_5            (1 << (AAD_FIXEDPOINT_DIGITS - 1))
#define AAD_LMSFILTER_MU              ((1 << AAD_FIXEDPOINT_DIGITS) >> 4)
#define AAD_PREEMPHASIS_SHIFT         5
#define AAD_PREEMPHASIS_NUMER         ((1 << AAD_PREEMPHASIS_SHIFT) - 1)

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

/* FourCCの一致確認 */
#define AAD_CHECK_FOURCC(u32lebuf, c1, c2, c3, c4) \
  ((u32lebuf) == ((c1 << 0) | (c2 << 8) | (c3 << 16) | (c4 << 24)))

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
struct AADCoreProcessor {
  uint8_t stepsize_index;         /* ステップサイズテーブルの参照インデックス     */

  int16_t history[4];
  int32_t weight[4];
};

/* デコーダ */
struct AADDecoder {
  struct AADHeaderInfo    header;
  struct AADCoreProcessor processor[AAD_MAX_NUM_CHANNELS];
  void                    *work;
};

/* エンコーダ */
struct AADEncoder {
  struct AADEncodeParameter encode_paramemter;
  uint8_t                   set_parameter;
  struct AADCoreProcessor   processor[AAD_MAX_NUM_CHANNELS];
  void                      *work;
};

/* 1サンプルデコード */
static int32_t AADCoreDecoder_DecodeSample(
    struct AADCoreProcessor *processor, uint8_t nibble);

/* モノラルブロックのデコード */
static AADError AADDecoder_DecodeBlockMono(
    struct AADCoreProcessor *processor,
    const uint8_t *read_pos, uint32_t data_size, 
    int32_t **buffer, uint32_t buffer_num_samples,
    uint32_t *num_decode_samples);

/* ステレオブロックのデコード */
static AADError AADDecoder_DecodeBlockStereo(
    struct AADCoreProcessor *processor,
    const uint8_t *read_pos, uint32_t data_size, 
    int32_t **buffer, uint32_t buffer_num_samples, 
    uint32_t *num_decode_samples);

/* 単一データブロックエンコード */
/* デコードとは違いstaticに縛る: エンコーダが内部的に状態を持ち、連続でEncodeBlockを呼ぶ必要があるから */
static AADApiResult AADEncoder_EncodeBlock(
    struct AADEncoder *encoder,
    const int32_t *const *input, uint32_t num_samples, 
    uint8_t *data, uint32_t data_size, uint32_t *output_size);

/* モノラルブロックのエンコード */
static AADError AADEncoder_EncodeBlockMono(
    struct AADCoreProcessor *processor,
    const int32_t *const *input, uint32_t num_samples,
    uint8_t *data, uint32_t data_size, uint32_t *output_size);

/* ステレオブロックのエンコード */
static AADError AADEncoder_EncodeBlockStereo(
    struct AADCoreProcessor *processor,
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
  uint32_t alloced_by_malloc = 0;

  /* 領域自前確保の場合 */
  if ((work == NULL) && (work_size == 0)) {
    work_size = AADDecoder_CalculateWorkSize();
    work = malloc((uint32_t)work_size);
    alloced_by_malloc = 1;
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

  /* 自前確保の場合はメモリを記憶しておく */
  decoder->work = alloced_by_malloc ? work : NULL;

  return decoder;
}

/* デコードハンドル破棄 */
void AADDecoder_Destroy(struct AADDecoder *decoder)
{
  if (decoder != NULL) {
    /* 自分で領域確保していたら破棄 */
    if (decoder->work != NULL) {
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
  uint32_t find_fact_chunk;
  struct AADHeaderInfo tmp_header_info;

  /* 引数チェック */
  if ((data == NULL) || (header_info == NULL)) {
    return AAD_APIRESULT_INVALID_ARGUMENT;
  }

  /* 読み出し用ポインタ設定 */
  data_pos = data;

  /* RIFFチャンクID */
  ByteArray_GetUint32LE(data_pos, &u32buf);
  if (!AAD_CHECK_FOURCC(u32buf, 'R', 'I', 'F', 'F')) {
    fprintf(stderr, "Invalid RIFF chunk id. \n");
    return AAD_APIRESULT_INVALID_FORMAT;
  }
  /* RIFFチャンクサイズ（読み飛ばし） */
  ByteArray_GetUint32LE(data_pos, &u32buf);

  /* WAVEチャンクID */
  ByteArray_GetUint32LE(data_pos, &u32buf);
  if (!AAD_CHECK_FOURCC(u32buf, 'W', 'A', 'V', 'E')) {
    fprintf(stderr, "Invalid WAVE chunk id. \n");
    return AAD_APIRESULT_INVALID_FORMAT;
  }

  /* FMTチャンクID */
  ByteArray_GetUint32LE(data_pos, &u32buf);
  if (!AAD_CHECK_FOURCC(u32buf, 'f', 'm', 't', ' ')) {
    fprintf(stderr, "Invalid fmt  chunk id. \n");
    return AAD_APIRESULT_INVALID_FORMAT;
  }
  /* fmtチャンクサイズ */
  ByteArray_GetUint32LE(data_pos, &u32buf);
  if (data_size <= u32buf) {
    fprintf(stderr, "Data size too small. fmt chunk size:%d data size:%d \n", u32buf, data_size);
    return AAD_APIRESULT_INSUFFICIENT_DATA;
  }
  /* WAVEフォーマットタイプ: IMA-ADPCM以外は受け付けない */
  ByteArray_GetUint16LE(data_pos, &u16buf);
  if (u16buf != 17) {
    fprintf(stderr, "Unsupported format: %d \n", u16buf);
    return AAD_APIRESULT_INVALID_FORMAT;
  }
  /* チャンネル数 */
  ByteArray_GetUint16LE(data_pos, &u16buf);
  if (u16buf > AAD_MAX_NUM_CHANNELS) {
    fprintf(stderr, "Unsupported channels: %d \n", u16buf);
    return AAD_APIRESULT_INVALID_FORMAT;
  }
  tmp_header_info.num_channels = u16buf;
  /* サンプリングレート */
  ByteArray_GetUint32LE(data_pos, &u32buf);
  tmp_header_info.sampling_rate = u32buf;
  /* データ速度[byte/sec] */
  ByteArray_GetUint32LE(data_pos, &u32buf);
  tmp_header_info.bytes_per_sec = u32buf;
  /* ブロックサイズ */
  ByteArray_GetUint16LE(data_pos, &u16buf);
  tmp_header_info.block_size = u16buf;
  /* サンプルあたりビット数 */
  ByteArray_GetUint16LE(data_pos, &u16buf);
  tmp_header_info.bits_per_sample = u16buf;
  /* fmtチャンクのエキストラサイズ: 2以外は想定していない */
  ByteArray_GetUint16LE(data_pos, &u16buf);
  if (u16buf != 2) {
    fprintf(stderr, "Unsupported fmt chunk extra size: %d \n", u16buf);
    return AAD_APIRESULT_INVALID_FORMAT;
  }
  /* ブロックあたりサンプル数 */
  ByteArray_GetUint16LE(data_pos, &u16buf);
  tmp_header_info.num_samples_per_block = u16buf;

  /* dataチャンクまで読み飛ばし */
  find_fact_chunk = 0;
  while (1) {
    uint32_t chunkid;
    /* サイズ超過 */
    if (data_size < (uint32_t)(data_pos - data)) {
      return AAD_APIRESULT_INSUFFICIENT_DATA;
    }
    /* チャンクID取得 */
    ByteArray_GetUint32LE(data_pos, &chunkid);
    if (AAD_CHECK_FOURCC(chunkid, 'd', 'a', 't', 'a')) {
      /* データチャンクを見つけたら終わり */
      break;
    } else if (AAD_CHECK_FOURCC(chunkid, 'f', 'a', 'c', 't')) {
      /* FACTチャンク（オプショナル） */
      ByteArray_GetUint32LE(data_pos, &u32buf);
      /* FACTチャンクサイズ: 4以外は想定していない */
      if (u32buf != 4) {
        fprintf(stderr, "Unsupported fact chunk size: %d \n", u16buf);
        return AAD_APIRESULT_INVALID_FORMAT;
      }
      /* サンプル数 */
      ByteArray_GetUint32LE(data_pos, &u32buf);
      tmp_header_info.num_samples = u32buf;
      /* factチャンクを見つけたことをマーク */
      assert(find_fact_chunk == 0);
      find_fact_chunk = 1;
    } else {
      uint32_t size;
      /* 他のチャンクはサイズだけ取得してシークにより読み飛ばす */
      ByteArray_GetUint32LE(data_pos, &size);
      /* printf("chunk:%8X size:%d \n", chunkid, (int32_t)size); */
      data_pos += size;
    }
  }

  /* データチャンクサイズ（読み飛ばし） */
  ByteArray_GetUint32LE(data_pos, &u32buf);

  /* factチャンクがない場合は、サンプル数をブロックサイズから計算 */
  if (find_fact_chunk == 0) {
    uint32_t data_chunk_size = u32buf;
    /* 末尾のブロック分も含めるため+1 */
    uint32_t num_blocks = data_chunk_size / tmp_header_info.block_size + 1;
    tmp_header_info.num_samples = tmp_header_info.num_samples_per_block * num_blocks;
  }

  /* データ領域先頭までのオフセット */
  tmp_header_info.header_size = (uint32_t)(data_pos - data);

  /* 成功終了 */
  (*header_info) = tmp_header_info;
  return AAD_APIRESULT_OK;
}

/* 1サンプルデコード */
static int32_t AADCoreDecoder_DecodeSample(
    struct AADCoreProcessor *processor, uint8_t nibble)
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
  /* memo:ffmpegを参考に、よくある分岐多用の実装はしない。
   * 分岐多用の実装は近似で結果がおかしいし、分岐ミスの方が負荷が大きいと判断 */
  delta = nibble & 7;
  qdiff = (stepsize * ((delta << 1) + 1)) >> 3;
  qdiff = (nibble & 8) ? -qdiff : qdiff; /* 符号ビットの反映 */
#if 0
  /* 3bit */
  delta = nibble & 3;
  qdiff = (stepsize * ((delta << 1) + 1)) >> 2;
  qdiff = (nibble & 4) ? -qdiff : qdiff;
  /* 2bit */
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
  sample = AAD_INNER_VAL(sample, -32768, 32767);

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

  /* フィルタ状態更新 */
  {
    int32_t delta_gain;

    /* 係数更新 */
    delta_gain = (qdiff > 0) ?  AAD_LMSFILTER_MU : qdiff;
    delta_gain = (qdiff < 0) ? -AAD_LMSFILTER_MU : qdiff;
    /*
    processor->weight[3] += (delta_gain * processor->history[3] + AAD_FIXEDPOINT_0_5) >> AAD_FIXEDPOINT_DIGITS;
    processor->weight[2] += (delta_gain * processor->history[2] + AAD_FIXEDPOINT_0_5) >> AAD_FIXEDPOINT_DIGITS;
    processor->weight[1] += (delta_gain * processor->history[1] + AAD_FIXEDPOINT_0_5) >> AAD_FIXEDPOINT_DIGITS;
    processor->weight[0] += (delta_gain * processor->history[0] + AAD_FIXEDPOINT_0_5) >> AAD_FIXEDPOINT_DIGITS;
    */
    processor->weight[3] += (qdiff * processor->history[3] + AAD_FIXEDPOINT_0_5) >> (AAD_FIXEDPOINT_DIGITS + 3);
    processor->weight[2] += (qdiff * processor->history[2] + AAD_FIXEDPOINT_0_5) >> (AAD_FIXEDPOINT_DIGITS + 3);
    processor->weight[1] += (qdiff * processor->history[1] + AAD_FIXEDPOINT_0_5) >> (AAD_FIXEDPOINT_DIGITS + 3);
    processor->weight[0] += (qdiff * processor->history[0] + AAD_FIXEDPOINT_0_5) >> (AAD_FIXEDPOINT_DIGITS + 3);

    /* 入力データ履歴更新 */
    processor->history[3] = processor->history[2];
    processor->history[2] = processor->history[1];
    processor->history[1] = processor->history[0];
    processor->history[0] = (int16_t)sample;
  }

  return sample;
}

/* モノラルブロックのデコード */
static AADError AADDecoder_DecodeBlockMono(
    struct AADCoreProcessor *processor,
    const uint8_t *read_pos, uint32_t data_size, 
    int32_t **buffer, uint32_t buffer_num_samples,
    uint32_t *num_decode_samples)
{
  uint8_t u8buf;
  uint8_t nibble[2];
  uint32_t smpl, tmp_num_decode_samples;
  const uint8_t *read_head = read_pos;

  /* 引数チェック */
  if ((processor == NULL) || (read_pos == NULL)
      || (buffer == NULL) || (buffer[0] == NULL)) {
    return AAD_ERROR_INVALID_ARGUMENT;
  }

  /* デコード可能なサンプル数を計算 *2は1バイトに2サンプル, +1はヘッダ分 */
  tmp_num_decode_samples = (data_size - 4) * 2;
  tmp_num_decode_samples += 1;
  /* バッファサイズで切り捨て */
  tmp_num_decode_samples = AAD_MIN_VAL(tmp_num_decode_samples, buffer_num_samples);

  /* ブロックヘッダデコード */
  {
    int16_t buf;
    /* ByteArray_GetUint16LE(read_pos, (uint16_t *)&(processor->history[0])); */ /* TODO:フィルタヒストリ全部記録せんとな... */
    ByteArray_GetUint16LE(read_pos, (uint16_t *)&buf);
    buffer[0][0] = buf;
  }
  ByteArray_GetUint8(read_pos, (uint8_t *)&(processor->stepsize_index)); 
  ByteArray_GetUint8(read_pos, &u8buf); /* reserved */
  if (u8buf != 0) {
    return AAD_ERROR_INVALID_FORMAT;
  }

  /* 先頭サンプルはヘッダに入っている */
  /* buffer[0][0] = (int16_t)processor->history[0]; */

  /* ブロックデータデコード */
  for (smpl = 1; smpl < tmp_num_decode_samples; smpl += 2) {
    assert((uint32_t)(read_pos - read_head) < data_size);
    ByteArray_GetUint8(read_pos, &u8buf);
    nibble[0] = (u8buf >> 0) & 0xF;
    nibble[1] = (u8buf >> 4) & 0xF;
    buffer[0][smpl + 0] = AADCoreDecoder_DecodeSample(processor, nibble[0]);
    buffer[0][smpl + 1] = AADCoreDecoder_DecodeSample(processor, nibble[1]);
  }

  /* デコードしたサンプル数をセット */
  (*num_decode_samples) = tmp_num_decode_samples;
  return AAD_ERROR_OK;
}

/* ステレオブロックのデコード */
static AADError AADDecoder_DecodeBlockStereo(
    struct AADCoreProcessor *processor,
    const uint8_t *read_pos, uint32_t data_size, 
    int32_t **buffer, uint32_t buffer_num_samples, 
    uint32_t *num_decode_samples)
{
  uint32_t u32buf;
  uint8_t nibble[8];
  uint32_t ch, smpl, tmp_num_decode_samples;
  const uint8_t *read_head = read_pos;

  /* 引数チェック */
  if ((processor == NULL) || (read_pos == NULL)
      || (buffer == NULL) || (buffer[0] == NULL) || (buffer[1] == NULL)) {
    return AAD_ERROR_INVALID_ARGUMENT;
  }

  /* デコード可能なサンプル数を計算 +1はヘッダ分 */
  tmp_num_decode_samples = data_size - 8;
  tmp_num_decode_samples += 1;
  /* バッファサイズで切り捨て */
  tmp_num_decode_samples = AAD_MIN_VAL(tmp_num_decode_samples, buffer_num_samples);

  /* ブロックヘッダデコード */
  for (ch = 0; ch < 2; ch++) {
    uint8_t reserved;
    int16_t buf;
    /* ByteArray_GetUint16LE(read_pos, (uint16_t *)&(processor[ch].history[0])); */ /* TODO:全ヒストリ記録スべし */
    ByteArray_GetUint16LE(read_pos, (uint16_t *)&buf); /* TODO:全ヒストリ記録スべし */
    buffer[ch][0] = buf;
    ByteArray_GetUint8(read_pos, (uint8_t *)&(processor[ch].stepsize_index));
    ByteArray_GetUint8(read_pos, &reserved);
    if (reserved != 0) {
      return AAD_ERROR_INVALID_FORMAT;
    }
  }

  /* 最初のサンプルの取得 */
  /*
  for (ch = 0; ch < 2; ch++) {
    buffer[ch][0] = processor[ch].history[0];
  }
  */

  /* ブロックデータデコード */
  for (smpl = 1; smpl < tmp_num_decode_samples; smpl += 8) {
    uint32_t smp;
    int32_t  buf[8];
    for (ch = 0; ch < 2; ch++) {
      assert((uint32_t)(read_pos - read_head) < data_size);
      ByteArray_GetUint32LE(read_pos, &u32buf);
      nibble[0] = (uint8_t)((u32buf >>  0) & 0xF);
      nibble[1] = (uint8_t)((u32buf >>  4) & 0xF);
      nibble[2] = (uint8_t)((u32buf >>  8) & 0xF);
      nibble[3] = (uint8_t)((u32buf >> 12) & 0xF);
      nibble[4] = (uint8_t)((u32buf >> 16) & 0xF);
      nibble[5] = (uint8_t)((u32buf >> 20) & 0xF);
      nibble[6] = (uint8_t)((u32buf >> 24) & 0xF);
      nibble[7] = (uint8_t)((u32buf >> 28) & 0xF);

      /* サンプル数が 1 + (8の倍数) でない場合があるため、一旦バッファに受ける */
      buf[0] = AADCoreDecoder_DecodeSample(&(processor[ch]), nibble[0]);
      buf[1] = AADCoreDecoder_DecodeSample(&(processor[ch]), nibble[1]);
      buf[2] = AADCoreDecoder_DecodeSample(&(processor[ch]), nibble[2]);
      buf[3] = AADCoreDecoder_DecodeSample(&(processor[ch]), nibble[3]);
      buf[4] = AADCoreDecoder_DecodeSample(&(processor[ch]), nibble[4]);
      buf[5] = AADCoreDecoder_DecodeSample(&(processor[ch]), nibble[5]);
      buf[6] = AADCoreDecoder_DecodeSample(&(processor[ch]), nibble[6]);
      buf[7] = AADCoreDecoder_DecodeSample(&(processor[ch]), nibble[7]);
      for (smp = 0; (smp < 8) && ((smpl + smp) < tmp_num_decode_samples); smp++) {
        buffer[ch][smpl + smp] = buf[smp];
      }
    }
  }

  /* MS -> LR */
  for (smpl = 0; smpl < tmp_num_decode_samples; smpl++) {
    int32_t mid, side;
    side = buffer[1][smpl];
    mid = ((int32_t)(buffer[0][smpl]) << 1) | (side & 1);
    buffer[0][smpl] = AAD_INNER_VAL((mid + side) >> 1, INT16_MIN, INT16_MAX);
    buffer[1][smpl] = AAD_INNER_VAL((mid - side) >> 1, INT16_MIN, INT16_MAX);
  }

  /* デコードしたサンプル数をセット */
  (*num_decode_samples) = tmp_num_decode_samples;
  return AAD_ERROR_OK;
}

/* 単一データブロックデコード */
static AADApiResult AADDecoder_DecodeBlock(
    struct AADDecoder *decoder,
    const uint8_t *data, uint32_t data_size, 
    int32_t **buffer, uint32_t buffer_num_channels, uint32_t buffer_num_samples, 
    uint32_t *num_decode_samples)
{
  AADError err;
  const struct AADHeaderInfo *header;

  /* 引数チェック */
  if ((decoder == NULL) || (data == NULL)
      || (buffer == NULL) || (num_decode_samples == NULL)) {
    return AAD_APIRESULT_INVALID_ARGUMENT;
  }

  header = &(decoder->header);

  /* バッファサイズチェック */
  if (buffer_num_channels < header->num_channels) {
    return AAD_APIRESULT_INSUFFICIENT_BUFFER;
  }

  /* ブロックデコード */
  switch (header->num_channels) {
    case 1:
      err = AADDecoder_DecodeBlockMono(decoder->processor, 
          data, data_size, buffer, buffer_num_samples, num_decode_samples);
      break;
    case 2:
      err = AADDecoder_DecodeBlockStereo(decoder->processor, 
          data, data_size, buffer, buffer_num_samples, num_decode_samples);
      break;
    default:
      return AAD_APIRESULT_INVALID_FORMAT;
  }

  /* デコード時のエラーハンドル */
  if (err != AAD_ERROR_OK) {
    switch (err) {
      case AAD_ERROR_INVALID_ARGUMENT:
        return AAD_APIRESULT_INVALID_ARGUMENT;
      case AAD_ERROR_INVALID_FORMAT:
        return AAD_APIRESULT_INVALID_FORMAT;
      case AAD_ERROR_INSUFFICIENT_BUFFER:
        return AAD_APIRESULT_INSUFFICIENT_BUFFER;
      default:
        return AAD_APIRESULT_NG;
    }
  }

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
  read_offset = header->header_size;
  read_pos = data + header->header_size;
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
  uint32_t num_blocks, data_chunk_size;
  uint32_t tail_block_num_samples, tail_block_size;

  /* 引数チェック */
  if ((header_info == NULL) || (data == NULL)) {
    return AAD_APIRESULT_INVALID_ARGUMENT;
  }

  /* ヘッダサイズと入力データサイズの比較 */
  if (data_size < AADENCODER_HEADER_SIZE) {
    return AAD_APIRESULT_INSUFFICIENT_DATA;
  }

  /* ヘッダの簡易チェック: ブロックサイズはサンプルデータを全て入れられるはず */
  if (AAD_CALCULATE_DATASIZE_BYTE(header_info->num_samples_per_block, header_info->bits_per_sample) > header_info->block_size) {
    return AAD_APIRESULT_INVALID_FORMAT;
  }
  
  /* データサイズ計算 */
  assert(header_info->num_samples_per_block != 0);
  num_blocks = (header_info->num_samples / header_info->num_samples_per_block) + 1;
  data_chunk_size = header_info->block_size * num_blocks;
  /* 末尾のブロックの剰余サンプルサイズだけ減じる */
  tail_block_num_samples = header_info->num_samples % header_info->num_samples_per_block;
  tail_block_size = AAD_CALCULATE_DATASIZE_BYTE(header_info->num_samples_per_block - tail_block_num_samples, header_info->bits_per_sample);
  data_chunk_size -= tail_block_size;

  /* 書き出し用ポインタ設定 */
  data_pos = data;

  /* RIFFチャンクID */
  ByteArray_PutUint8(data_pos, 'R');
  ByteArray_PutUint8(data_pos, 'I');
  ByteArray_PutUint8(data_pos, 'F');
  ByteArray_PutUint8(data_pos, 'F');
  /* RIFFチャンクサイズ */
  ByteArray_PutUint32LE(data_pos, AADENCODER_HEADER_SIZE + data_chunk_size - 8);
  /* WAVEチャンクID */
  ByteArray_PutUint8(data_pos, 'W');
  ByteArray_PutUint8(data_pos, 'A');
  ByteArray_PutUint8(data_pos, 'V');
  ByteArray_PutUint8(data_pos, 'E');
  /* FMTチャンクID */
  ByteArray_PutUint8(data_pos, 'f');
  ByteArray_PutUint8(data_pos, 'm');
  ByteArray_PutUint8(data_pos, 't');
  ByteArray_PutUint8(data_pos, ' ');
  /* FMTチャンクサイズは20で決め打ち */
  ByteArray_PutUint32LE(data_pos, 20);
  /* WAVEフォーマットタイプ: IMA-ADPCM(17)で決め打ち */
  ByteArray_PutUint16LE(data_pos, 17);
  /* チャンネル数 */
  if (header_info->num_channels > AAD_MAX_NUM_CHANNELS) {
    return AAD_APIRESULT_INVALID_FORMAT;
  }
  ByteArray_PutUint16LE(data_pos, header_info->num_channels);
  /* サンプリングレート */
  ByteArray_PutUint32LE(data_pos, header_info->sampling_rate);
  /* データ速度[byte/sec] */
  ByteArray_PutUint32LE(data_pos, header_info->bytes_per_sec);
  /* ブロックサイズ */
  ByteArray_PutUint16LE(data_pos, header_info->block_size);
  /* サンプルあたりビット数 */
  if ((header_info->bits_per_sample == 0)
      || (header_info->bits_per_sample > AAD_MAX_BITS_PER_SAMPLE)) {
    return AAD_APIRESULT_INVALID_FORMAT;
  }
  ByteArray_PutUint16LE(data_pos, header_info->bits_per_sample);
  /* fmtチャンクのエキストラサイズ: 2で決め打ち */
  ByteArray_PutUint16LE(data_pos, 2);
  /* ブロックあたりサンプル数 */
  ByteArray_PutUint16LE(data_pos, header_info->num_samples_per_block);

  /* FACTチャンクID */
  ByteArray_PutUint8(data_pos, 'f');
  ByteArray_PutUint8(data_pos, 'a');
  ByteArray_PutUint8(data_pos, 'c');
  ByteArray_PutUint8(data_pos, 't');
  /* FACTチャンクのエキストラサイズ: 4で決め打ち */
  ByteArray_PutUint32LE(data_pos, 4);
  /* サンプル数 */
  ByteArray_PutUint32LE(data_pos, header_info->num_samples);

  /* その他のチャンクは書き出さず、すぐにdataチャンクへ */

  /* dataチャンクID */
  ByteArray_PutUint8(data_pos, 'd');
  ByteArray_PutUint8(data_pos, 'a');
  ByteArray_PutUint8(data_pos, 't');
  ByteArray_PutUint8(data_pos, 'a');
  /* データチャンクサイズ */
  ByteArray_PutUint32LE(data_pos, data_chunk_size);

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
  uint32_t alloced_by_malloc = 0;

  /* 領域自前確保の場合 */
  if ((work == NULL) && (work_size == 0)) {
    work_size = AADEncoder_CalculateWorkSize();
    work = malloc((uint32_t)work_size);
    alloced_by_malloc = 1;
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

  /* 自前確保の場合はメモリを記憶しておく */
  encoder->work = alloced_by_malloc ? work : NULL;

  return encoder;
}

/* エンコーダハンドル破棄 */
void AADEncoder_Destroy(struct AADEncoder *encoder)
{
  if (encoder != NULL) {
    /* 自分で領域確保していたら破棄 */
    if (encoder->work != NULL) {
      free(encoder->work);
    }
  }
}

/* 1サンプルエンコード */
static uint8_t AADCoreEncoder_EncodeSample(
    struct AADCoreProcessor *processor, int32_t sample)
{
  uint8_t nibble;
  int16_t idx;
  int32_t predict, diff, qdiff, delta, stepsize, diffabs, sign;
  static int32_t q_error = 0;

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
  diff -= (q_error >> 2); /* 量子化誤差をフィードバック（ノイズシェーピング） */
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
  delta = nibble & 3;
  qdiff = (stepsize * ((delta << 1) + 1)) >> 2;
  qdiff = (nibble & 4) ? -qdiff : qdiff;
  /* 2bit */
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

  /* フィルタ状態更新 */
  {
    int32_t delta_gain;
    int32_t qsample;

    /* 量子化後のサンプル値 */
    qsample = qdiff + predict;
    /* 16bit幅にクリップ */
    qsample = AAD_INNER_VAL(qsample, -32768, 32767);

    /* 誤差の量子化誤差 */
    q_error = diff - qdiff;

    /* 係数更新 */
    delta_gain = (qdiff > 0) ?  AAD_LMSFILTER_MU : qdiff;
    delta_gain = (qdiff < 0) ? -AAD_LMSFILTER_MU : qdiff;
    /*
    processor->weight[3] += (delta_gain * processor->history[3] + AAD_FIXEDPOINT_0_5) >> AAD_FIXEDPOINT_DIGITS;
    processor->weight[2] += (delta_gain * processor->history[2] + AAD_FIXEDPOINT_0_5) >> AAD_FIXEDPOINT_DIGITS;
    processor->weight[1] += (delta_gain * processor->history[1] + AAD_FIXEDPOINT_0_5) >> AAD_FIXEDPOINT_DIGITS;
    processor->weight[0] += (delta_gain * processor->history[0] + AAD_FIXEDPOINT_0_5) >> AAD_FIXEDPOINT_DIGITS;
    */
    processor->weight[3] += (qdiff * processor->history[3] + AAD_FIXEDPOINT_0_5) >> (AAD_FIXEDPOINT_DIGITS + 3);
    processor->weight[2] += (qdiff * processor->history[2] + AAD_FIXEDPOINT_0_5) >> (AAD_FIXEDPOINT_DIGITS + 3);
    processor->weight[1] += (qdiff * processor->history[1] + AAD_FIXEDPOINT_0_5) >> (AAD_FIXEDPOINT_DIGITS + 3);
    processor->weight[0] += (qdiff * processor->history[0] + AAD_FIXEDPOINT_0_5) >> (AAD_FIXEDPOINT_DIGITS + 3);

    /* 入力データ履歴更新 */
    processor->history[3] = processor->history[2];
    processor->history[2] = processor->history[1];
    processor->history[1] = processor->history[0];
    processor->history[0] = (int16_t)qsample;

    {
      static uint32_t count = 0;
      static int32_t prevsample = 0;
      static double mabserr = 0.0f, preverr = 0.0f;

      count++;
      mabserr += fabs((double)(sample - qsample)) / INT16_MAX;
      preverr += fabs((double)(sample - prevsample)) / INT16_MAX;
      prevsample = sample;

      if (count % 1000000 == 0) {
        printf("%f %f %d \n", mabserr / count, preverr / count, nibble & 7);
      }
      /* printf("%d %d %d \n", idx, stepsize, qdiff); */
    }

    /* printf("%d %d \n", sample, qsample); */
  }

  return nibble;
}

/* モノラルブロックのエンコード */
static AADError AADEncoder_EncodeBlockMono(
    struct AADCoreProcessor *processor,
    const int32_t *const *input, uint32_t num_samples,
    uint8_t *data, uint32_t data_size, uint32_t *output_size)
{
  uint8_t u8buf;
  uint8_t nibble[2];
  uint32_t smpl;
  uint8_t *data_pos = data;

  /* 引数チェック */
  if ((processor == NULL) || (input == NULL)
      || (data == NULL) || (output_size == NULL)) {
    return AAD_ERROR_INVALID_ARGUMENT;
  }

  /* 十分なデータサイズがあるか確認 */
  if (data_size < (num_samples / 2 + 4)) {
    return AAD_ERROR_INSUFFICIENT_DATA;
  }

  /* 先頭サンプルをエンコーダにセット */
  /* processor->history[0] = input[0][0]; */

  /* ブロックヘッダエンコード */
  ByteArray_PutUint16LE(data_pos, processor->history[0]); /* TODO: 全ヒストリ記録 */
  ByteArray_PutUint8(data_pos, processor->stepsize_index);
  ByteArray_PutUint8(data_pos, 0); /* reserved */

  /* ブロックデータエンコード */
  for (smpl = 1; smpl < num_samples; smpl += 2) {
    assert((uint32_t)(data_pos - data) < data_size);
    nibble[0] = AADCoreEncoder_EncodeSample(processor, input[0][smpl + 0]);
    nibble[1] = AADCoreEncoder_EncodeSample(processor, input[0][smpl + 1]);
    assert((nibble[0] <= 0xF) && (nibble[1] <= 0xF));
    u8buf = (uint8_t)((nibble[0] << 0) | (nibble[1] << 4));
    ByteArray_PutUint8(data_pos, u8buf);
  }

  /* 書き出しサイズをセット */
  (*output_size) = (uint32_t)(data_pos - data);
  return AAD_ERROR_OK;
}

/* ステレオブロックのエンコード */
static AADError AADEncoder_EncodeBlockStereo(
    struct AADCoreProcessor *processor,
    const int32_t *const *input, uint32_t num_samples,
    uint8_t *data, uint32_t data_size, uint32_t *output_size)
{
  uint32_t u32buf;
  uint8_t nibble[8];
  uint32_t ch, smpl;
  uint8_t *data_pos = data;

  /* 引数チェック */
  if ((processor == NULL) || (input == NULL)
      || (data == NULL) || (output_size == NULL)) {
    return AAD_ERROR_INVALID_ARGUMENT;
  }

  /* 十分なデータサイズがあるか確認 */
  if (data_size < (num_samples + 4)) {
    return AAD_ERROR_INSUFFICIENT_DATA;
  }

  /* 先頭サンプルをエンコーダにセット */
#if 0
  for (ch = 0; ch < 2; ch++) {
    processor[ch].history[0] = input[ch][0];
  }
#endif

  /* ブロックヘッダエンコード */
  for (ch = 0; ch < 2; ch++) {
    ByteArray_PutUint16LE(data_pos, processor[ch].history[0]);      /* TODO:全ヒストリ記録 */
    ByteArray_PutUint8(data_pos,    processor[ch].stepsize_index);
    ByteArray_PutUint8(data_pos, 0); /* reserved */
  }

  /* ブロックデータエンコード */
  for (smpl = 1; smpl < num_samples; smpl += 8) {
    int32_t inbuf[2][8];
    /* LR -> MS */
    {
      uint32_t smp;
      for (smp = 0; smp < 8; smp++) {
        int32_t mid, side;
        mid = (input[0][smp + smpl] + input[1][smp + smpl]) >> 1;
        side = input[0][smp + smpl] - input[1][smp + smpl];
        inbuf[0][smp] = mid;
        inbuf[1][smp] = side;
      }
    }
    for (ch = 0; ch < 2; ch++) {
      assert((uint32_t)(data_pos - data) < data_size);
      nibble[0] = AADCoreEncoder_EncodeSample(&(processor[ch]), inbuf[ch][0]);
      nibble[1] = AADCoreEncoder_EncodeSample(&(processor[ch]), inbuf[ch][1]);
      nibble[2] = AADCoreEncoder_EncodeSample(&(processor[ch]), inbuf[ch][2]);
      nibble[3] = AADCoreEncoder_EncodeSample(&(processor[ch]), inbuf[ch][3]);
      nibble[4] = AADCoreEncoder_EncodeSample(&(processor[ch]), inbuf[ch][4]);
      nibble[5] = AADCoreEncoder_EncodeSample(&(processor[ch]), inbuf[ch][5]);
      nibble[6] = AADCoreEncoder_EncodeSample(&(processor[ch]), inbuf[ch][6]);
      nibble[7] = AADCoreEncoder_EncodeSample(&(processor[ch]), inbuf[ch][7]);
      assert((nibble[0] <= 0xF) && (nibble[1] <= 0xF) && (nibble[2] <= 0xF) && (nibble[3] <= 0xF)
          && (nibble[4] <= 0xF) && (nibble[5] <= 0xF) && (nibble[6] <= 0xF) && (nibble[7] <= 0xF));
      u32buf  = (uint32_t)(nibble[0] <<  0);
      u32buf |= (uint32_t)(nibble[1] <<  4);
      u32buf |= (uint32_t)(nibble[2] <<  8);
      u32buf |= (uint32_t)(nibble[3] << 12);
      u32buf |= (uint32_t)(nibble[4] << 16);
      u32buf |= (uint32_t)(nibble[5] << 20);
      u32buf |= (uint32_t)(nibble[6] << 24);
      u32buf |= (uint32_t)(nibble[7] << 28);
      ByteArray_PutUint32LE(data_pos, u32buf);
    }
  }

  /* 書き出しサイズをセット */
  (*output_size) = (uint32_t)(data_pos - data);
  return AAD_ERROR_OK;
}

/* 単一データブロックエンコード */
static AADApiResult AADEncoder_EncodeBlock(
    struct AADEncoder *encoder,
    const int32_t *const *input, uint32_t num_samples, 
    uint8_t *data, uint32_t data_size, uint32_t *output_size)
{
  AADError err;
  const struct AADEncodeParameter *enc_param;

  /* 引数チェック */
  if ((encoder == NULL) || (data == NULL)
      || (input == NULL) || (output_size == NULL)) {
    return AAD_APIRESULT_INVALID_ARGUMENT;
  }
  enc_param = &(encoder->encode_paramemter);

  /* ブロックデコード */
  switch (enc_param->num_channels) {
    case 1:
      err = AADEncoder_EncodeBlockMono(encoder->processor, 
          input, num_samples, data, data_size, output_size);
      break;
    case 2:
      err = AADEncoder_EncodeBlockStereo(encoder->processor, 
          input, num_samples, data, data_size, output_size);
      break;
    default:
      return AAD_APIRESULT_INVALID_FORMAT;
  }

  /* デコード時のエラーハンドル */
  if (err != AAD_ERROR_OK) {
    switch (err) {
      case AAD_ERROR_INVALID_ARGUMENT:
        return AAD_APIRESULT_INVALID_ARGUMENT;
      case AAD_ERROR_INVALID_FORMAT:
        return AAD_APIRESULT_INVALID_FORMAT;
      case AAD_ERROR_INSUFFICIENT_BUFFER:
        return AAD_APIRESULT_INSUFFICIENT_BUFFER;
      default:
        return AAD_APIRESULT_NG;
    }
  }

  return AAD_APIRESULT_OK;
}

/* エンコードパラメータをヘッダに変換 */
static AADError AADEncoder_ConvertParameterToHeader(
    const struct AADEncodeParameter *enc_param, uint32_t num_samples,
    struct AADHeaderInfo *header_info)
{
  uint32_t block_data_size;
  struct AADHeaderInfo tmp_header = {0, };

  /* 引数チェック */
  if ((enc_param == NULL) || (header_info == NULL)) {
    return AAD_ERROR_INVALID_ARGUMENT;
  }

  /* サンプルあたりビット数のチェック */
  if ((enc_param->bits_per_sample == 0)
      || (enc_param->bits_per_sample > AAD_MAX_BITS_PER_SAMPLE)) {
    return AAD_ERROR_INVALID_FORMAT;
  }

  /* ヘッダサイズは決め打ち */
  tmp_header.header_size = AADENCODER_HEADER_SIZE;
  /* 総サンプル数 */
  tmp_header.num_samples = num_samples;

  /* そのままヘッダに入れられるメンバ */
  tmp_header.num_channels = enc_param->num_channels;
  tmp_header.sampling_rate = enc_param->sampling_rate;
  tmp_header.bits_per_sample = enc_param->bits_per_sample;
  tmp_header.block_size = enc_param->block_size;

  /* 計算が必要なメンバ */
  if (enc_param->block_size <= enc_param->num_channels * 4) {
    /* データを入れる領域がない */
    return AAD_ERROR_INVALID_FORMAT;
  }
  /* 4はチャンネルあたりのヘッダ領域サイズ */
  assert(enc_param->block_size >= (enc_param->num_channels * 4));
  block_data_size = (uint32_t)(enc_param->block_size - (enc_param->num_channels * 4));
  assert((block_data_size * 8) % (uint32_t)(enc_param->bits_per_sample * enc_param->num_channels) == 0);
  assert((enc_param->bits_per_sample * enc_param->num_channels) != 0);
  tmp_header.num_samples_per_block = (uint16_t)((block_data_size * 8) / (uint32_t)(enc_param->bits_per_sample * enc_param->num_channels));
  /* ヘッダに入っている分+1 */
  tmp_header.num_samples_per_block++;
  assert(tmp_header.num_samples_per_block != 0);
  tmp_header.bytes_per_sec = (enc_param->block_size * enc_param->sampling_rate) / tmp_header.num_samples_per_block;

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

  /* パラメータ設定 */
  encoder->encode_paramemter = (*parameter);

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
  struct AADHeaderInfo header = { 0, };

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

  /* エンコードパラメータをヘッダに変換 */
  if (AADEncoder_ConvertParameterToHeader(&(encoder->encode_paramemter), num_samples, &header) != AAD_ERROR_OK) {
    return AAD_APIRESULT_INVALID_FORMAT;
  }

  /* ヘッダエンコード */
  if ((ret = AADEncoder_EncodeHeader(&header, data_pos, data_size)) != AAD_APIRESULT_OK) {
    return ret;
  }

  progress = 0;
  write_offset = AADENCODER_HEADER_SIZE;
  data_pos = data + AADENCODER_HEADER_SIZE;
  while (progress < num_samples) {
    /* エンコードサンプル数の確定 */
    num_encode_samples 
      = AAD_MIN_VAL(header.num_samples_per_block, num_samples - progress);
    /* サンプル参照位置のセット */
    for (ch = 0; ch < header.num_channels; ch++) {
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
    assert(write_size <= header.block_size);
    assert(write_offset <= data_size);
  }

  /* 成功終了 */
  (*output_size) = write_offset;
  return AAD_APIRESULT_OK;
}

