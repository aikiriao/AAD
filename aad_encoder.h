#ifndef AAD_ENCODER_H_INCLDED
#define AAD_ENCODER_H_INCLDED

#include "aad.h"
#include <stdint.h>

/* エンコードパラメータ */
struct AADEncodeParameter {
  uint16_t num_channels;          /* チャンネル数                                 */
  uint32_t sampling_rate;         /* サンプリングレート                           */
  uint16_t bits_per_sample;       /* サンプルあたりビット数（今の所4で固定）      */
  uint16_t max_block_size;        /* 最大ブロックサイズ[byte]                     */
};

/* エンコーダハンドル */
struct AADEncoder;

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* ブロックサイズとブロックあたりサンプル数の計算 */
AADApiResult AADEncoder_CalculateBlockSize(
    uint32_t max_block_size, uint16_t num_channels, uint32_t bits_per_sample,
    uint16_t *block_size, uint32_t *num_samples_per_block);

/* ヘッダエンコード */
AADApiResult AADEncoder_EncodeHeader(
    const struct AADHeaderInfo *header_info, uint8_t *data, uint32_t data_size);

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

#endif /* AAD_ENCODER_H_INCLDED */
