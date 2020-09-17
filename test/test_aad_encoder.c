#include "test.h"
#include <stdlib.h>
#include <string.h>

/* テスト対象のモジュール */
#include "../aad_encoder.c"

/* テストのセットアップ関数 */
void AADEncoderTest_Setup(void);

static int AADEncoderTest_Initialize(void *obj)
{
  TEST_UNUSED_PARAMETER(obj);
  return 0;
}

static int AADEncoderTest_Finalize(void *obj)
{
  TEST_UNUSED_PARAMETER(obj);
  return 0;
}

/* ブロックサイズ計算テスト */
static void AADEncoderTest_CalculateBlockSizeTest(void *obj)
{
  TEST_UNUSED_PARAMETER(obj);

  /* 計算値チェック */
  {
    uint16_t block_size;
    uint32_t num_samples_per_block;

    Test_AssertEqual(AADEncoder_CalculateBlockSize(32, 1, 4, &block_size, &num_samples_per_block), AAD_APIRESULT_OK);
    Test_AssertEqual(block_size, 32); Test_AssertEqual(num_samples_per_block, 32);
    Test_AssertEqual(AADEncoder_CalculateBlockSize(64, 2, 4, &block_size, &num_samples_per_block), AAD_APIRESULT_OK);
    Test_AssertEqual(block_size, 64); Test_AssertEqual(num_samples_per_block, 32);

    /* 3bitは特殊なので多めに確認 */
    Test_AssertEqual(AADEncoder_CalculateBlockSize(  64, 1, 3, &block_size, &num_samples_per_block), AAD_APIRESULT_OK);
    Test_AssertEqual(block_size,   63); Test_AssertEqual(num_samples_per_block,  124);
    Test_AssertEqual(AADEncoder_CalculateBlockSize(  64, 2, 3, &block_size, &num_samples_per_block), AAD_APIRESULT_OK);
    Test_AssertEqual(block_size,   60); Test_AssertEqual(num_samples_per_block,   36);
    Test_AssertEqual(AADEncoder_CalculateBlockSize( 128, 1, 3, &block_size, &num_samples_per_block), AAD_APIRESULT_OK);
    Test_AssertEqual(block_size,  126); Test_AssertEqual(num_samples_per_block,  292);
    Test_AssertEqual(AADEncoder_CalculateBlockSize( 128, 2, 3, &block_size, &num_samples_per_block), AAD_APIRESULT_OK);
    Test_AssertEqual(block_size,  126); Test_AssertEqual(num_samples_per_block,  124);
    Test_AssertEqual(AADEncoder_CalculateBlockSize(1024, 1, 3, &block_size, &num_samples_per_block), AAD_APIRESULT_OK);
    Test_AssertEqual(block_size, 1023); Test_AssertEqual(num_samples_per_block, 2684);
    Test_AssertEqual(AADEncoder_CalculateBlockSize(1024, 2, 3, &block_size, &num_samples_per_block), AAD_APIRESULT_OK);
    Test_AssertEqual(block_size, 1020); Test_AssertEqual(num_samples_per_block, 1316);

    Test_AssertEqual(AADEncoder_CalculateBlockSize(32, 1, 2, &block_size, &num_samples_per_block), AAD_APIRESULT_OK);
    Test_AssertEqual(block_size, 32); Test_AssertEqual(num_samples_per_block,  60);
    Test_AssertEqual(AADEncoder_CalculateBlockSize(64, 1, 2, &block_size, &num_samples_per_block), AAD_APIRESULT_OK);
    Test_AssertEqual(block_size, 64); Test_AssertEqual(num_samples_per_block, 188);
    Test_AssertEqual(AADEncoder_CalculateBlockSize(64, 2, 2, &block_size, &num_samples_per_block), AAD_APIRESULT_OK);
    Test_AssertEqual(block_size, 64); Test_AssertEqual(num_samples_per_block,  60);

    /* サンプル数はNULLでも良しとしている */
    Test_AssertEqual(AADEncoder_CalculateBlockSize(32, 1, 4, &block_size, NULL), AAD_APIRESULT_OK);
    Test_AssertEqual(block_size, 32); 
    Test_AssertEqual(AADEncoder_CalculateBlockSize(64, 2, 4, &block_size, NULL), AAD_APIRESULT_OK);
    Test_AssertEqual(block_size, 64);
  }

  /* 失敗例 */
  {
    uint16_t block_size;
    uint32_t num_samples_per_block;

    /* 引数が不正 */
    Test_AssertEqual(AADEncoder_CalculateBlockSize(32, 1, 4, NULL, NULL), AAD_APIRESULT_INVALID_ARGUMENT);
    Test_AssertEqual(AADEncoder_CalculateBlockSize(32, 1, 4, NULL, &num_samples_per_block), AAD_APIRESULT_INVALID_ARGUMENT);
    /* フォーマットが不正 */
    Test_AssertEqual(AADEncoder_CalculateBlockSize(AAD_BLOCK_HEADER_SIZE(1) - 1, 1, 4, &block_size, &num_samples_per_block), AAD_APIRESULT_INVALID_FORMAT);
    Test_AssertEqual(AADEncoder_CalculateBlockSize(32, 0, 4, &block_size, &num_samples_per_block), AAD_APIRESULT_INVALID_FORMAT);
    Test_AssertEqual(AADEncoder_CalculateBlockSize(32, AAD_MAX_NUM_CHANNELS + 1, 4, &block_size, &num_samples_per_block), AAD_APIRESULT_INVALID_FORMAT);
    Test_AssertEqual(AADEncoder_CalculateBlockSize(32, 1, 0, &block_size, &num_samples_per_block), AAD_APIRESULT_INVALID_FORMAT);
    Test_AssertEqual(AADEncoder_CalculateBlockSize(32, 1, AAD_MAX_BITS_PER_SAMPLE + 1, &block_size, &num_samples_per_block), AAD_APIRESULT_INVALID_FORMAT);
  }
}

/* ヘッダエンコードデコードテスト */
static void AADEncoderTest_HeaderEncodeTest(void *obj)
{
  TEST_UNUSED_PARAMETER(obj);

  /* 有効なヘッダをセット */
#define AAD_SetValidHeader(p_header) {                          \
  struct AADHeaderInfo *header__p = p_header;                   \
  header__p->num_channels           = 1;                        \
  header__p->sampling_rate          = 44100;                    \
  header__p->block_size             = 32;                       \
  header__p->bits_per_sample        = AAD_MAX_BITS_PER_SAMPLE;  \
  header__p->num_samples            = 1024;                     \
  header__p->num_samples_per_block  = 32;                       \
}

  /* ヘッダエンコード成功ケース */
  {
    struct AADHeaderInfo header;
    uint8_t data[AAD_HEADER_SIZE] = { 0, };

    AAD_SetValidHeader(&header);
    Test_AssertEqual(AADEncoder_EncodeHeader(&header, data, sizeof(data)), AAD_APIRESULT_OK);

    /* 簡易チェック */
    Test_AssertEqual(data[0], 'A');
    Test_AssertEqual(data[1], 'A');
    Test_AssertEqual(data[2], 'D');
    Test_AssertEqual(data[3], '\0');
  }

  /* ヘッダエンコード失敗ケース */
  {
    struct AADHeaderInfo header;
    uint8_t data[AAD_HEADER_SIZE] = { 0, };

    /* 引数が不正 */
    AAD_SetValidHeader(&header);
    Test_AssertEqual(AADEncoder_EncodeHeader(NULL, data, sizeof(data)), AAD_APIRESULT_INVALID_ARGUMENT);
    Test_AssertEqual(AADEncoder_EncodeHeader(&header, NULL, sizeof(data)), AAD_APIRESULT_INVALID_ARGUMENT);

    /* データサイズ不足 */
    AAD_SetValidHeader(&header);
    Test_AssertEqual(AADEncoder_EncodeHeader(&header, data, sizeof(data) - 1), AAD_APIRESULT_INSUFFICIENT_DATA);
    Test_AssertEqual(AADEncoder_EncodeHeader(&header, data, AAD_HEADER_SIZE - 1), AAD_APIRESULT_INSUFFICIENT_DATA);

    /* 異常なチャンネル数 */
    AAD_SetValidHeader(&header);
    header.num_channels = 0;
    Test_AssertEqual(AADEncoder_EncodeHeader(&header, data, sizeof(data)), AAD_APIRESULT_INVALID_FORMAT);
    AAD_SetValidHeader(&header);
    header.num_channels = AAD_MAX_NUM_CHANNELS + 1;
    Test_AssertEqual(AADEncoder_EncodeHeader(&header, data, sizeof(data)), AAD_APIRESULT_INVALID_FORMAT);

    /* 異常なサンプル数 */
    AAD_SetValidHeader(&header);
    header.num_samples = 0;
    Test_AssertEqual(AADEncoder_EncodeHeader(&header, data, sizeof(data)), AAD_APIRESULT_INVALID_FORMAT);

    /* 異常なサンプリングレート */
    AAD_SetValidHeader(&header);
    header.sampling_rate = 0;
    Test_AssertEqual(AADEncoder_EncodeHeader(&header, data, sizeof(data)), AAD_APIRESULT_INVALID_FORMAT);

    /* 異常なビット深度 */
    AAD_SetValidHeader(&header);
    header.bits_per_sample = 0;
    Test_AssertEqual(AADEncoder_EncodeHeader(&header, data, sizeof(data)), AAD_APIRESULT_INVALID_FORMAT);
    AAD_SetValidHeader(&header);
    header.bits_per_sample = AAD_MAX_BITS_PER_SAMPLE + 1;
    Test_AssertEqual(AADEncoder_EncodeHeader(&header, data, sizeof(data)), AAD_APIRESULT_INVALID_FORMAT);

    /* 異常なブロックサイズ */
    AAD_SetValidHeader(&header);
    header.block_size = 0;
    Test_AssertEqual(AADEncoder_EncodeHeader(&header, data, sizeof(data)), AAD_APIRESULT_INVALID_FORMAT);
    AAD_SetValidHeader(&header);
    header.block_size = AAD_BLOCK_HEADER_SIZE(header.num_channels) - 1;
    Test_AssertEqual(AADEncoder_EncodeHeader(&header, data, sizeof(data)), AAD_APIRESULT_INVALID_FORMAT);

    /* 異常なブロックあたりサンプル数 */
    AAD_SetValidHeader(&header);
    header.num_samples_per_block = 0;
    Test_AssertEqual(AADEncoder_EncodeHeader(&header, data, sizeof(data)), AAD_APIRESULT_INVALID_FORMAT);
  }

}

/* エンコードハンドル作成破棄テスト */
static void AADEncoderTest_CreateDestroyTest(void *obj)
{
  TEST_UNUSED_PARAMETER(obj);

  /* ワークサイズ計算テスト */
  {
    int32_t work_size;

    work_size = AADEncoder_CalculateWorkSize();
    Test_AssertCondition(work_size >= (int32_t)sizeof(struct AADEncoder));
  }

  /* ワーク領域渡しによるハンドル作成（成功例） */
  {
    void *work;
    int32_t work_size;
    struct AADEncoder *encoder;

    work_size = AADEncoder_CalculateWorkSize();
    work = malloc(work_size);

    encoder = AADEncoder_Create(work, work_size);
    Test_AssertCondition(encoder != NULL);
    Test_AssertCondition(encoder->work == work);
    Test_AssertCondition(encoder->set_parameter == 0);
    Test_AssertCondition(encoder->alloced_by_own != 1);

    AADEncoder_Destroy(encoder);
    free(work);
  }

  /* 自前確保によるハンドル作成（成功例） */
  {
    struct AADEncoder *encoder;

    encoder = AADEncoder_Create(NULL, 0);
    Test_AssertCondition(encoder != NULL);
    Test_AssertCondition(encoder->work != NULL);
    Test_AssertCondition(encoder->alloced_by_own == 1);

    AADEncoder_Destroy(encoder);
  }

  /* ワーク領域渡しによるハンドル作成（失敗ケース） */
  {
    void *work;
    int32_t work_size;
    struct AADEncoder *encoder;

    work_size = AADEncoder_CalculateWorkSize();
    work = malloc(work_size);

    /* 引数が不正 */
    encoder = AADEncoder_Create(NULL, work_size);
    Test_AssertCondition(encoder == NULL);
    encoder = AADEncoder_Create(work, 0);
    Test_AssertCondition(encoder == NULL);

    /* ワークサイズ不足 */
    encoder = AADEncoder_Create(work, work_size - 1);
    Test_AssertCondition(encoder == NULL);

    free(work);
  }
}

/* エンコードパラメータ設定テスト */
static void AADEncoderTest_SetEncodeParameterTest(void *obj)
{
  TEST_UNUSED_PARAMETER(obj);

  /* 有効なパラメータをセット */
#define AAD_SetValidParameter(p_param) {            \
    struct AADEncodeParameter *p__param = p_param;  \
    p__param->num_channels    = 1;                  \
    p__param->sampling_rate   = 8000;               \
    p__param->bits_per_sample = 4;                  \
    p__param->max_block_size  = 256;                \
}

  /* 成功例 */
  {
    struct AADEncoder *encoder;
    struct AADEncodeParameter param;
    struct AADHeaderInfo header;

    encoder = AADEncoder_Create(NULL, 0);
    
    AAD_SetValidParameter(&param);
    Test_AssertEqual(AADEncoder_ConvertParameterToHeader(&param, 0, &header), AAD_APIRESULT_OK);

    Test_AssertEqual(AADEncoder_SetEncodeParameter(encoder, &param), AAD_APIRESULT_OK);
    Test_AssertEqual(encoder->set_parameter, 1);
    Test_AssertEqual(memcmp(&(encoder->header), &header, sizeof(struct AADHeaderInfo)), 0);

    AADEncoder_Destroy(encoder);
  }

  /* 失敗ケース */
  {
    struct AADEncoder *encoder;
    struct AADEncodeParameter param;

    encoder = AADEncoder_Create(NULL, 0);

    /* 引数が不正 */
    AAD_SetValidParameter(&param);
    Test_AssertEqual(AADEncoder_SetEncodeParameter(NULL,  &param), AAD_APIRESULT_INVALID_ARGUMENT);
    Test_AssertEqual(AADEncoder_SetEncodeParameter(encoder, NULL), AAD_APIRESULT_INVALID_ARGUMENT);

    /* サンプルあたりビット数が異常 */
    AAD_SetValidParameter(&param);
    param.bits_per_sample = 0;
    Test_AssertEqual(AADEncoder_SetEncodeParameter(encoder, &param), AAD_APIRESULT_INVALID_FORMAT);
    AAD_SetValidParameter(&param);
    param.bits_per_sample = AAD_MAX_BITS_PER_SAMPLE + 1;
    Test_AssertEqual(AADEncoder_SetEncodeParameter(encoder, &param), AAD_APIRESULT_INVALID_FORMAT);

    /* ブロックサイズが小さすぎる */
    AAD_SetValidParameter(&param);
    param.max_block_size = 0;
    Test_AssertEqual(AADEncoder_SetEncodeParameter(encoder, &param), AAD_APIRESULT_INVALID_FORMAT);
    AAD_SetValidParameter(&param);
    param.max_block_size = AAD_BLOCK_HEADER_SIZE(param.num_channels) - 1;
    Test_AssertEqual(AADEncoder_SetEncodeParameter(encoder, &param), AAD_APIRESULT_INVALID_FORMAT);

    AADEncoder_Destroy(encoder);
  }
}

void AADEncoderTest_Setup(void)
{
  struct TestSuite *suite
    = Test_AddTestSuite("AAD Encoder Test Suite",
        NULL, AADEncoderTest_Initialize, AADEncoderTest_Finalize);

  Test_AddTest(suite, AADEncoderTest_HeaderEncodeTest);
  Test_AddTest(suite, AADEncoderTest_CalculateBlockSizeTest);
  Test_AddTest(suite, AADEncoderTest_CreateDestroyTest);
  Test_AddTest(suite, AADEncoderTest_SetEncodeParameterTest);
}
