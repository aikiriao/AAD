#ifndef AAD_H_INCLDED
#define AAD_H_INCLDED

#include <stdint.h>

/* コーデックバージョン */
#define AAD_CODEC_VERSION           11

/* フォーマットバージョン */
#define AAD_FORMAT_VERSION          4

/* 処理可能な最大チャンネル数 */
#define AAD_MAX_NUM_CHANNELS        2

/* 最小のサンプルあたりビット数 */
#define AAD_MIN_BITS_PER_SAMPLE     2

/* 最大のサンプルあたりビット数 */
#define AAD_MAX_BITS_PER_SAMPLE     4

/* ヘッダサイズ[byte] */
#define AAD_HEADER_SIZE             31

/* API結果型 */
typedef enum AADApiResultTag {
  AAD_APIRESULT_OK = 0,              /* 成功                         */
  AAD_APIRESULT_INVALID_ARGUMENT,    /* 無効な引数                   */
  AAD_APIRESULT_INVALID_FORMAT,      /* 不正なフォーマット           */
  AAD_APIRESULT_INSUFFICIENT_BUFFER, /* バッファサイズが足りない     */
  AAD_APIRESULT_INSUFFICIENT_DATA,   /* データが足りない             */
  AAD_APIRESULT_PARAMETER_NOT_SET,   /* パラメータがセットされてない */
  AAD_APIRESULT_NG                   /* 分類不能な失敗               */
} AADApiResult; 

/* マルチチャンネル処理法 */
typedef enum AADChannelProcessMethodTag {
  AAD_CH_PROCESS_METHOD_NONE = 0,  /* 何もしない     */
  AAD_CH_PROCESS_METHOD_MS,        /* ステレオMS処理 */
  AAD_CH_PROCESS_METHOD_INVALID    /* 無効値         */
} AADChannelProcessMethod;

/* ヘッダ情報 */
struct AADHeaderInfo {
  uint16_t num_channels;                      /* チャンネル数                   */
  uint32_t num_samples;                       /* 1チャンネルあたり総サンプル数  */
  uint32_t sampling_rate;                     /* サンプリングレート             */
  uint16_t bits_per_sample;                   /* サンプルあたりビット数         */
  uint16_t block_size;                        /* ブロックサイズ                 */
  uint32_t num_samples_per_block;             /* ブロックあたりサンプル数       */
  AADChannelProcessMethod ch_process_method;  /* マルチチャンネル処理法         */
};

#endif /* AAD_H_INCLDED */
