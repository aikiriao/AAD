#ifndef AAD_H_INCLDED
#define AAD_H_INCLDED

#include <stdint.h>


/* フォーマットバージョン */
#define AAD_FORMAT_VERSION          1

/* 処理可能な最大チャンネル数 */
#define AAD_MAX_NUM_CHANNELS        2

/* 最大のサンプルあたりビット数 */
#define AAD_MAX_BITS_PER_SAMPLE     4

/* ヘッダサイズ[byte] */
#define AAD_HEADER_SIZE             26

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

/* ヘッダ情報 */
struct AADHeaderInfo {
  uint16_t num_channels;          /* チャンネル数                                 */
  uint32_t num_samples;           /* 1チャンネルあたり総サンプル数                */
  uint32_t sampling_rate;         /* サンプリングレート                           */
  uint16_t bits_per_sample;       /* サンプルあたりビット数                       */
  uint16_t block_size;            /* ブロックサイズ                               */
  uint32_t num_samples_per_block; /* ブロックあたりサンプル数                     */
};

/* エンコードパラメータ */
struct AADEncodeParameter {
  uint16_t num_channels;          /* チャンネル数                                 */
  uint32_t sampling_rate;         /* サンプリングレート                           */
  uint16_t bits_per_sample;       /* サンプルあたりビット数（今の所4で固定）      */
  uint16_t max_block_size;        /* 最大ブロックサイズ[byte]                     */
};

/* デコーダハンドル */
struct AADDecoder;

/* エンコーダハンドル */
struct AADEncoder;

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* ブロックサイズとブロックあたりサンプル数の計算 */
AADApiResult AADEncoder_CalculateBlockSize(
    uint32_t max_block_size, uint16_t num_channels, uint32_t bits_per_sample,
    uint16_t *block_size, uint32_t *num_samples_per_block);

/* ヘッダデコード */
AADApiResult AADDecoder_DecodeHeader(
    const uint8_t *data, uint32_t data_size, struct AADHeaderInfo *header_info);

/* ヘッダエンコード */
AADApiResult AADEncoder_EncodeHeader(
    const struct AADHeaderInfo *header_info, uint8_t *data, uint32_t data_size);

/* デコーダワークサイズ計算 */
int32_t AADDecoder_CalculateWorkSize(void);

/* デコーダハンドル作成 */
struct AADDecoder *AADDecoder_Create(void *work, int32_t work_size);

/* デコーダハンドル破棄 */
void AADDecoder_Destroy(struct AADDecoder *decoder);

/* ヘッダ含めファイル全体をデコード */
AADApiResult AADDecoder_DecodeWhole(
    struct AADDecoder *decoder,
    const uint8_t *data, uint32_t data_size,
    int32_t **buffer, uint32_t buffer_num_channels, uint32_t buffer_num_samples);

/* エンコーダワークサイズ計算 */
int32_t AADEncoder_CalculateWorkSize(void);

/* エンコーダハンドル作成 */
struct AADEncoder *AADEncoder_Create(void *work, int32_t work_size);

/* エンコーダハンドル破棄 */
void AADEncoder_Destroy(struct AADEncoder *encoder);

/* エンコードパラメータの設定 */
AADApiResult AADEncoder_SetEncodeParameter(
    struct AADEncoder *encoder, const struct AADEncodeParameter *parameter);

/* ヘッダ含めファイル全体をエンコード */
AADApiResult AADEncoder_EncodeWhole(
    struct AADEncoder *encoder,
    const int32_t *const *input, uint32_t num_samples,
    uint8_t *data, uint32_t data_size, uint32_t *output_size);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* AAD_H_INCLDED */
