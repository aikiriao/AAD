#ifndef AAD_INTERNAL_H_INCLUDED
#define AAD_INTERNAL_H_INCLUDED

#include <assert.h>

/* メモリアラインメント */
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

/* テーブル要素数の計算 */
#define AAD_NUM_TABLE_ELEMENTS(array) (sizeof(array) / sizeof(array[0]))

/* ブロックヘッダサイズの計算 */
#define AAD_BLOCK_HEADER_SIZE(num_channels) (18 * (num_channels))

/* 指定データサイズ内に含まれるサンプル数を計算 */
#define AAD_NUM_SAMPLES_IN_DATA(data_size, num_channels, bits_per_sample) \
  ((data_size) * 8) / ((num_channels) * (bits_per_sample))

/* ブロック内に含まれるサンプル数を計算 */
#define AAD_NUM_SAMPLES_IN_BLOCK(data_size, num_channels, bits_per_sample) \
  (AAD_FILTER_ORDER + AAD_NUM_SAMPLES_IN_DATA((data_size) - AAD_BLOCK_HEADER_SIZE(num_channels), (num_channels), (bits_per_sample)))

/* アサートマクロ */
#ifdef DEBUG
#define AAD_ASSERT(condition) assert(condition)
#else
#define AAD_ASSERT(condition) (void)(condition)
#endif

/* 内部エラー型 */
typedef enum AADErrorTag {
  AAD_ERROR_OK = 0,              /* OK */
  AAD_ERROR_NG,                  /* 分類不能な失敗 */
  AAD_ERROR_INVALID_ARGUMENT,    /* 不正な引数 */
  AAD_ERROR_INVALID_FORMAT,      /* 不正なフォーマット       */
  AAD_ERROR_INSUFFICIENT_BUFFER, /* バッファサイズが足りない */
  AAD_ERROR_INSUFFICIENT_DATA    /* データサイズが足りない   */
} AADError;

#endif /* AAD_INTERNAL_H_INCLUDED */
