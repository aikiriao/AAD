#ifndef AAD_DECODER_H_INCLDED
#define AAD_DECODER_H_INCLDED

#include "aad.h"
#include <stdint.h>

/* デコーダハンドル */
struct AADDecoder;

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* ヘッダデコード */
AADApiResult AADDecoder_DecodeHeader(
    const uint8_t *data, uint32_t data_size, struct AADHeaderInfo *header_info);

/* デコーダワークサイズ計算 */
int32_t AADDecoder_CalculateWorkSize(void);

/* デコーダハンドル作成 */
struct AADDecoder *AADDecoder_Create(void *work, int32_t work_size);

/* デコーダハンドル破棄 */
void AADDecoder_Destroy(struct AADDecoder *decoder);

/* 単一データブロックデコード */
AADApiResult AADDecoder_DecodeBlock(
    struct AADDecoder *decoder,
    const uint8_t *data, uint32_t data_size, 
    int32_t **buffer, uint32_t buffer_num_channels, uint32_t buffer_num_samples, 
    uint32_t *num_decode_samples);

/* ヘッダ含めファイル全体をデコード */
AADApiResult AADDecoder_DecodeWhole(
    struct AADDecoder *decoder,
    const uint8_t *data, uint32_t data_size,
    int32_t **buffer, uint32_t buffer_num_channels, uint32_t buffer_num_samples);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* AAD_DECODER_H_INCLDED */
