#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <assert.h>
#include <math.h>
#include "test.h"

/* テスト対象のモジュール */
#include "../aad.c"

/* 追加でwavを使用 */
#include "../wav.c"

/* テストのセットアップ関数 */
void AADTest_Setup(void);

static int AADTest_Initialize(void *obj)
{
  TEST_UNUSED_PARAMETER(obj);
  return 0;
}

static int AADTest_Finalize(void *obj)
{
  TEST_UNUSED_PARAMETER(obj);
  return 0;
}

/* ブロックサイズ計算テスト */
static void AADTest_CalculateBlockSizeTest(void *obj)
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
static void AADTest_HeaderEncodeDecodeTest(void *obj)
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

  /* 成功例 */
  {
    uint8_t data[AAD_HEADER_SIZE] = { 0, };
    struct AADHeaderInfo header = { 0, }, tmp_header = { 0, };

    AAD_SetValidHeader(&header);

    /* エンコード->デコード */
    Test_AssertEqual(AADEncoder_EncodeHeader(&header, data, sizeof(data)), AAD_APIRESULT_OK);
    Test_AssertEqual(AADDecoder_DecodeHeader(data, sizeof(data), &tmp_header), AAD_APIRESULT_OK);

    /* デコードしたヘッダの一致確認 */
    Test_AssertEqual(header.num_channels,           tmp_header.num_channels);
    Test_AssertEqual(header.sampling_rate,          tmp_header.sampling_rate);
    Test_AssertEqual(header.block_size,             tmp_header.block_size);
    Test_AssertEqual(header.bits_per_sample,        tmp_header.bits_per_sample);
    Test_AssertEqual(header.num_samples,            tmp_header.num_samples);
    Test_AssertEqual(header.num_samples_per_block,  tmp_header.num_samples_per_block);
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

  /* ヘッダデコード失敗ケース */
  {
    struct AADHeaderInfo header, getheader;
    uint8_t valid_data[AAD_HEADER_SIZE] = { 0, };
    uint8_t data[AAD_HEADER_SIZE];

    /* 有効な内容を作っておく */
    AAD_SetValidHeader(&header);
    AADEncoder_EncodeHeader(&header, valid_data, sizeof(valid_data));

    /* シグネチャが不正 */
    memcpy(data, valid_data, sizeof(valid_data));
    ByteArray_WriteUint8(&data[0], 'a');
    Test_AssertEqual(AADDecoder_DecodeHeader(data, sizeof(data), &getheader), AAD_APIRESULT_INVALID_FORMAT);

    /* 異常なフォーマットバージョン */
    memcpy(data, valid_data, sizeof(valid_data));
    ByteArray_WriteUint32BE(&data[4], 0);
    Test_AssertEqual(AADDecoder_DecodeHeader(data, sizeof(data), &getheader), AAD_APIRESULT_INVALID_FORMAT);
    memcpy(data, valid_data, sizeof(valid_data));
    ByteArray_WriteUint32BE(&data[4], AAD_FORMAT_VERSION + 1);
    Test_AssertEqual(AADDecoder_DecodeHeader(data, sizeof(data), &getheader), AAD_APIRESULT_INVALID_FORMAT);

    /* 異常なチャンネル数 */
    memcpy(data, valid_data, sizeof(valid_data));
    ByteArray_WriteUint16BE(&data[8], 0);
    Test_AssertEqual(AADDecoder_DecodeHeader(data, sizeof(data), &getheader), AAD_APIRESULT_INVALID_FORMAT);
    memcpy(data, valid_data, sizeof(valid_data));
    ByteArray_WriteUint16BE(&data[8], AAD_MAX_NUM_CHANNELS + 1);
    Test_AssertEqual(AADDecoder_DecodeHeader(data, sizeof(data), &getheader), AAD_APIRESULT_INVALID_FORMAT);

    /* 異常なサンプル数 */
    memcpy(data, valid_data, sizeof(valid_data));
    ByteArray_WriteUint32BE(&data[10], 0);
    Test_AssertEqual(AADDecoder_DecodeHeader(data, sizeof(data), &getheader), AAD_APIRESULT_INVALID_FORMAT);

    /* 異常なサンプリングレート */
    memcpy(data, valid_data, sizeof(valid_data));
    ByteArray_WriteUint32BE(&data[14], 0);
    Test_AssertEqual(AADDecoder_DecodeHeader(data, sizeof(data), &getheader), AAD_APIRESULT_INVALID_FORMAT);

    /* 異常なサンプルあたりビット数 */
    memcpy(data, valid_data, sizeof(valid_data));
    ByteArray_WriteUint16BE(&data[18], 0);
    Test_AssertEqual(AADDecoder_DecodeHeader(data, sizeof(data), &getheader), AAD_APIRESULT_INVALID_FORMAT);
    memcpy(data, valid_data, sizeof(valid_data));
    ByteArray_WriteUint16BE(&data[18], AAD_MAX_BITS_PER_SAMPLE + 1);
    Test_AssertEqual(AADDecoder_DecodeHeader(data, sizeof(data), &getheader), AAD_APIRESULT_INVALID_FORMAT);

    /* 異常なブロックサイズ */
    memcpy(data, valid_data, sizeof(valid_data));
    ByteArray_WriteUint16BE(&data[20], 0);
    Test_AssertEqual(AADDecoder_DecodeHeader(data, sizeof(data), &getheader), AAD_APIRESULT_INVALID_FORMAT);
    memcpy(data, valid_data, sizeof(valid_data));
    ByteArray_WriteUint16BE(&data[20], AAD_BLOCK_HEADER_SIZE(header.num_channels) - 1);
    Test_AssertEqual(AADDecoder_DecodeHeader(data, sizeof(data), &getheader), AAD_APIRESULT_INVALID_FORMAT);
  }
}

/* デコードハンドル作成破棄テスト */
static void AADDecoderTest_CreateDestroyTest(void *obj)
{
  TEST_UNUSED_PARAMETER(obj);

  /* ワークサイズ計算テスト */
  {
    int32_t work_size;

    work_size = AADDecoder_CalculateWorkSize();
    Test_AssertCondition(work_size >= (int32_t)sizeof(struct AADDecoder));
  }

  /* ワーク領域渡しによるハンドル作成（成功例） */
  {
    void *work;
    int32_t work_size;
    struct AADDecoder *decoder;

    work_size = AADDecoder_CalculateWorkSize();
    work = malloc(work_size);

    decoder = AADDecoder_Create(work, work_size);
    Test_AssertCondition(decoder != NULL);
    Test_AssertCondition(decoder->work == work);
    Test_AssertCondition(decoder->alloced_by_own != 1);

    AADDecoder_Destroy(decoder);
    free(work);
  }

  /* 自前確保によるハンドル作成（成功例） */
  {
    struct AADDecoder *decoder;

    decoder = AADDecoder_Create(NULL, 0);
    Test_AssertCondition(decoder != NULL);
    Test_AssertCondition(decoder->work != NULL);
    Test_AssertCondition(decoder->alloced_by_own == 1);

    AADDecoder_Destroy(decoder);
  }

  /* ワーク領域渡しによるハンドル作成（失敗ケース） */
  {
    void *work;
    int32_t work_size;
    struct AADDecoder *decoder;

    work_size = AADDecoder_CalculateWorkSize();
    work = malloc(work_size);

    /* 引数が不正 */
    decoder = AADDecoder_Create(NULL, work_size);
    Test_AssertCondition(decoder == NULL);
    decoder = AADDecoder_Create(work, 0);
    Test_AssertCondition(decoder == NULL);

    /* ワークサイズ不足 */
    decoder = AADDecoder_Create(work, work_size - 1);
    Test_AssertCondition(decoder == NULL);

    free(work);
  }
}

/* デコードテスト */
static void AADDecoderTest_DecodeTest(void *obj)
{
  TEST_UNUSED_PARAMETER(obj);
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

/* 与えられたPCMデータに対するエンコードデコードテスト 成功時は1を、失敗時は0を返す */
static uint8_t AADTest_EncodeDecodeCheckForPcmData(
    int32_t **input, 
    uint32_t num_channels, uint32_t num_samples,
    uint16_t bits_per_sample, uint16_t block_size, double rms_epsilon)
{
  int32_t *decoded[AAD_MAX_NUM_CHANNELS];
  uint32_t ch, smpl, buffer_size, output_size;
  uint8_t *buffer;
  double rms_error;
  uint8_t is_ok;
  struct AADEncodeParameter enc_param;
  struct AADEncoder *encoder;
  struct AADDecoder *decoder;

  assert(input != NULL);
  assert(input[0] != NULL);
  assert(num_channels <= AAD_MAX_NUM_CHANNELS);

  /* 出力データの領域割当て */
  for (ch = 0; ch < num_channels; ch++) {
    decoded[ch] = (int32_t *)malloc(sizeof(int32_t) * num_samples);
  }
  /* 入力wavと同じサイズの出力領域を確保（増えることはないと期待） */
  buffer_size = num_channels * num_samples * sizeof(int32_t);
  buffer = (uint8_t *)malloc(buffer_size);

  /* 出力データ領域をクリア */
  for (ch = 0; ch < num_channels; ch++) {
    for (smpl = 0; smpl < num_samples; smpl++) {
      decoded[ch][smpl] = 0;
    }
  }

  /* ハンドル作成 */
  encoder = AADEncoder_Create(NULL, 0);
  decoder = AADDecoder_Create(NULL, 0);

  /* エンコードパラメータをセット */
  enc_param.num_channels    = num_channels;
  enc_param.sampling_rate   = 8000;
  enc_param.bits_per_sample = bits_per_sample;
  enc_param.max_block_size  = block_size;
  if (AADEncoder_SetEncodeParameter(encoder, &enc_param) != AAD_APIRESULT_OK) {
    is_ok = 0;
    goto CHECK_END;
  }

  /* エンコード */
  if (AADEncoder_EncodeWhole(
        encoder, (const int32_t* const*)input, num_samples, buffer, buffer_size, &output_size) != AAD_APIRESULT_OK) {
    is_ok = 0;
    goto CHECK_END;
  }
  /* 半分以下にはなるはず */
  if (output_size >= (buffer_size / 2)) {
    is_ok = 0;
    goto CHECK_END;
  }

  /* デコード */
  if (AADDecoder_DecodeWhole(
        decoder, buffer, output_size, decoded, num_channels, num_samples) != AAD_APIRESULT_OK) {
    is_ok = 0;
    goto CHECK_END;
  }

  /* ロスがあるのでRMSE基準でチェック */
  rms_error = 0.0;
  for (ch = 0; ch < num_channels; ch++) {
    for (smpl = 0; smpl < num_samples; smpl++) {
      double pcm1, pcm2, abs_error;
      pcm1 = (double)input[ch][smpl] / INT16_MAX;
      pcm2 = (double)decoded[ch][smpl] / INT16_MAX;
      abs_error = fabs(pcm1 - pcm2);
      rms_error += abs_error * abs_error;
    }
  }
  rms_error = sqrt(rms_error / (num_samples * num_channels));

  /* 誤差チェック */
  if (rms_error >= rms_epsilon) {
    is_ok = 0;
    goto CHECK_END;
  }

  /* ここまできたらOK */
  is_ok = 1;

CHECK_END:
  /* 領域開放 */
  AADEncoder_Destroy(encoder);
  AADDecoder_Destroy(decoder);
  free(buffer);
  for (ch = 0; ch < num_channels; ch++) {
    free(decoded[ch]);
  }

  return is_ok;
}

/* エンコード→デコードチェックルーチン 成功時は1, 失敗時は0を返す */
static uint8_t AADTest_EncodeDecodeCheckForWavFile(
    const char *wav_filename, uint16_t bits_per_sample, uint16_t block_size, double rms_epsilon)
{
  struct WAVFile *wavfile;
  struct stat fstat;
  int32_t *input[AAD_MAX_NUM_CHANNELS];
  int32_t *decoded[AAD_MAX_NUM_CHANNELS];
  uint8_t is_ok;
  uint32_t ch, smpl, buffer_size, output_size;
  uint32_t num_channels, num_samples;
  uint8_t *buffer;
  double rms_error;
  struct AADEncodeParameter enc_param;
  struct AADEncoder *encoder;
  struct AADDecoder *decoder;

  assert((wav_filename != NULL) && (rms_epsilon >= 0.0f));

  /* 入力wav取得 */
  wavfile = WAV_CreateFromFile(wav_filename);
  assert(wavfile != NULL);
  num_channels = wavfile->format.num_channels;
  num_samples = wavfile->format.num_samples;

  /* 入出力データの領域割当て */
  for (ch = 0; ch < num_channels; ch++) {
    input[ch]   = malloc(sizeof(int32_t) * num_samples);
    decoded[ch] = malloc(sizeof(int32_t) * num_samples);
  }
  /* 入力wavと同じサイズの出力領域を確保（増えることはないと期待） */
  stat(wav_filename, &fstat);
  buffer_size = fstat.st_size;
  buffer = malloc(buffer_size);

  /* 16bit幅でデータ取得 */
  for (ch = 0; ch < num_channels; ch++) {
    for (smpl = 0; smpl < num_samples; smpl++) {
      input[ch][smpl] = WAVFile_PCM(wavfile, smpl, ch) >> 16;
    }
  }

  /* ハンドル作成 */
  encoder = AADEncoder_Create(NULL, 0);
  decoder = AADDecoder_Create(NULL, 0);

  /* エンコードパラメータをセット */
  enc_param.num_channels    = num_channels;
  enc_param.sampling_rate   = wavfile->format.sampling_rate;
  enc_param.bits_per_sample = bits_per_sample;
  enc_param.max_block_size  = block_size;
  if (AADEncoder_SetEncodeParameter(encoder, &enc_param) != AAD_APIRESULT_OK) {
    is_ok = 0;
    goto CHECK_END;
  }

  /* エンコード */
  if (AADEncoder_EncodeWhole(
        encoder, (const int32_t *const *)input, num_samples,
        buffer, buffer_size, &output_size) != AAD_APIRESULT_OK) {
    is_ok = 0;
    goto CHECK_END;
  }
  /* 半分以下にはなるはず */
  if (output_size >= (buffer_size / 2)) {
    is_ok = 0;
    goto CHECK_END;
  }

  /* デコード */
  if (AADDecoder_DecodeWhole(
        decoder, buffer, output_size,
        decoded, num_channels, num_samples) != AAD_APIRESULT_OK) {
    is_ok = 0;
    goto CHECK_END;
  }

  /* ロスがあるのでRMSE基準でチェック */
  rms_error = 0.0;
  for (ch = 0; ch < num_channels; ch++) {
    for (smpl = 0; smpl < num_samples; smpl++) {
      double pcm1, pcm2, abs_error;
      pcm1 = (double)input[ch][smpl] / INT16_MAX;
      pcm2 = (double)decoded[ch][smpl] / INT16_MAX;
      abs_error = fabs(pcm1 - pcm2);
      rms_error += abs_error * abs_error;
    }
  }
  rms_error = sqrt(rms_error / (num_samples * num_channels));

  /* マージンチェック */
  if (rms_error >= rms_epsilon) {
    is_ok = 0;
    goto CHECK_END;
  } 

  /* ここまできたらOK */
  is_ok = 1;

CHECK_END:
  /* 領域開放 */
  AADEncoder_Destroy(encoder);
  AADDecoder_Destroy(decoder);
  free(buffer);
  for (ch = 0; ch < num_channels; ch++) {
    free(input[ch]);
    free(decoded[ch]);
  }

  return is_ok;
}

/* エンコードデコードテスト */
static void AADTest_EncodeDecodeTest(void *obj)
{
  TEST_UNUSED_PARAMETER(obj);

  /* PCMデータをエンコード->デコードしてみて、誤差が許容範囲内に収まるか？ */
  {
#define MAX_NUM_CHANNELS  AAD_MAX_NUM_CHANNELS
#define MAX_NUM_SAMPLES   2048
    int32_t *input[MAX_NUM_CHANNELS];
    uint32_t ch, smpl;

    /* 出力データの領域割当て */
    for (ch = 0; ch < MAX_NUM_CHANNELS; ch++) {
      input[ch] = (int32_t *)malloc(sizeof(int32_t) * MAX_NUM_SAMPLES);
    }

    /* 正弦波作成 */
    for (ch = 0; ch < MAX_NUM_CHANNELS; ch++) {
      for (smpl = 0; smpl < MAX_NUM_SAMPLES; smpl++) {
        input[ch][smpl] = INT16_MAX * 0.5 * sin((2.0 * 3.1415 * 440.0 * smpl) / 48000.0);
      }
    }
    
    /* チェック関数の実行 */
    Test_AssertEqual(AADTest_EncodeDecodeCheckForPcmData(input, 1, MAX_NUM_SAMPLES, 4,  128, 5.0e-2), 1);
    Test_AssertEqual(AADTest_EncodeDecodeCheckForPcmData(input, 2, MAX_NUM_SAMPLES, 4,  128, 5.0e-2), 1);
    Test_AssertEqual(AADTest_EncodeDecodeCheckForPcmData(input, 1, MAX_NUM_SAMPLES, 4, 1024, 5.0e-2), 1);
    Test_AssertEqual(AADTest_EncodeDecodeCheckForPcmData(input, 2, MAX_NUM_SAMPLES, 4, 1024, 5.0e-2), 1);
    Test_AssertEqual(AADTest_EncodeDecodeCheckForPcmData(input, 1, MAX_NUM_SAMPLES, 3,  128, 6.0e-2), 1);
    Test_AssertEqual(AADTest_EncodeDecodeCheckForPcmData(input, 2, MAX_NUM_SAMPLES, 3,  128, 6.0e-2), 1);
    Test_AssertEqual(AADTest_EncodeDecodeCheckForPcmData(input, 1, MAX_NUM_SAMPLES, 3, 1024, 6.0e-2), 1);
    Test_AssertEqual(AADTest_EncodeDecodeCheckForPcmData(input, 2, MAX_NUM_SAMPLES, 3, 1024, 6.0e-2), 1);
    Test_AssertEqual(AADTest_EncodeDecodeCheckForPcmData(input, 1, MAX_NUM_SAMPLES, 2,  128, 8.0e-2), 1);
    Test_AssertEqual(AADTest_EncodeDecodeCheckForPcmData(input, 2, MAX_NUM_SAMPLES, 2,  128, 8.0e-2), 1);
    Test_AssertEqual(AADTest_EncodeDecodeCheckForPcmData(input, 1, MAX_NUM_SAMPLES, 2, 1024, 8.0e-2), 1);
    Test_AssertEqual(AADTest_EncodeDecodeCheckForPcmData(input, 2, MAX_NUM_SAMPLES, 2, 1024, 8.0e-2), 1);

    /* 白色雑音作成 */
    srand(0);
    for (ch = 0; ch < MAX_NUM_CHANNELS; ch++) {
      for (smpl = 0; smpl < MAX_NUM_SAMPLES; smpl++) {
        input[ch][smpl] = INT16_MAX * 2.0f * ((double)rand() / RAND_MAX - 0.5f);
      }
    }
    
    /* チェック */
    Test_AssertEqual(AADTest_EncodeDecodeCheckForPcmData(input, 1, MAX_NUM_SAMPLES, 4,  128, 1.0e-1), 1);
    Test_AssertEqual(AADTest_EncodeDecodeCheckForPcmData(input, 2, MAX_NUM_SAMPLES, 4,  128, 1.0e-1), 1);
    Test_AssertEqual(AADTest_EncodeDecodeCheckForPcmData(input, 1, MAX_NUM_SAMPLES, 4, 1024, 1.0e-1), 1);
    Test_AssertEqual(AADTest_EncodeDecodeCheckForPcmData(input, 2, MAX_NUM_SAMPLES, 4, 1024, 1.0e-1), 1);
    Test_AssertEqual(AADTest_EncodeDecodeCheckForPcmData(input, 1, MAX_NUM_SAMPLES, 3,  128, 1.5e-1), 1);
    Test_AssertEqual(AADTest_EncodeDecodeCheckForPcmData(input, 2, MAX_NUM_SAMPLES, 3,  128, 1.5e-1), 1);
    Test_AssertEqual(AADTest_EncodeDecodeCheckForPcmData(input, 1, MAX_NUM_SAMPLES, 3, 1024, 1.5e-1), 1);
    Test_AssertEqual(AADTest_EncodeDecodeCheckForPcmData(input, 2, MAX_NUM_SAMPLES, 3, 1024, 1.5e-1), 1);
    Test_AssertEqual(AADTest_EncodeDecodeCheckForPcmData(input, 1, MAX_NUM_SAMPLES, 2,  128, 2.4e-1), 1);
    Test_AssertEqual(AADTest_EncodeDecodeCheckForPcmData(input, 2, MAX_NUM_SAMPLES, 2,  128, 2.4e-1), 1);
    Test_AssertEqual(AADTest_EncodeDecodeCheckForPcmData(input, 1, MAX_NUM_SAMPLES, 2, 1024, 2.4e-1), 1);
    Test_AssertEqual(AADTest_EncodeDecodeCheckForPcmData(input, 2, MAX_NUM_SAMPLES, 2, 1024, 2.4e-1), 1);

    /* ナイキストレート振動波 */
    for (ch = 0; ch < MAX_NUM_CHANNELS; ch++) {
      for (smpl = 0; smpl < MAX_NUM_SAMPLES; smpl++) {
        input[ch][smpl] = (smpl % 2) ? INT16_MIN : INT16_MAX;
      }
    }
    
    /* チェック */
    Test_AssertEqual(AADTest_EncodeDecodeCheckForPcmData(input, 1, MAX_NUM_SAMPLES, 4,  128, 1.2e-1), 1);
    Test_AssertEqual(AADTest_EncodeDecodeCheckForPcmData(input, 2, MAX_NUM_SAMPLES, 4,  128, 1.2e-1), 1);
    Test_AssertEqual(AADTest_EncodeDecodeCheckForPcmData(input, 1, MAX_NUM_SAMPLES, 4, 1024, 1.2e-1), 1);
    Test_AssertEqual(AADTest_EncodeDecodeCheckForPcmData(input, 2, MAX_NUM_SAMPLES, 4, 1024, 1.2e-1), 1);
    Test_AssertEqual(AADTest_EncodeDecodeCheckForPcmData(input, 1, MAX_NUM_SAMPLES, 3,  128, 1.6e-1), 1);
    Test_AssertEqual(AADTest_EncodeDecodeCheckForPcmData(input, 2, MAX_NUM_SAMPLES, 3,  128, 1.6e-1), 1);
    Test_AssertEqual(AADTest_EncodeDecodeCheckForPcmData(input, 1, MAX_NUM_SAMPLES, 3, 1024, 1.6e-1), 1);
    Test_AssertEqual(AADTest_EncodeDecodeCheckForPcmData(input, 2, MAX_NUM_SAMPLES, 3, 1024, 1.6e-1), 1);
    Test_AssertEqual(AADTest_EncodeDecodeCheckForPcmData(input, 1, MAX_NUM_SAMPLES, 2,  128, 2.3e-1), 1);
    Test_AssertEqual(AADTest_EncodeDecodeCheckForPcmData(input, 2, MAX_NUM_SAMPLES, 2,  128, 2.3e-1), 1);
    Test_AssertEqual(AADTest_EncodeDecodeCheckForPcmData(input, 1, MAX_NUM_SAMPLES, 2, 1024, 2.3e-1), 1);
    Test_AssertEqual(AADTest_EncodeDecodeCheckForPcmData(input, 2, MAX_NUM_SAMPLES, 2, 1024, 2.3e-1), 1);

    for (ch = 0; ch < MAX_NUM_CHANNELS; ch++) {
      free(input[ch]);
    }
#undef MAX_NUM_CHANNELS
#undef NUM_SAMPLES
  }

  /* wavファイルのエンコードデコードテスト */
  {
    uint32_t  i;
    uint8_t   is_ok;

    /* テストケース */
    struct EncodeDecodeTestForWavFileTestCase {
      const char  *filename;
      uint16_t    bits_per_sample;
      uint16_t    block_size;
      double      rms_epsilon;
    };

    /* テストケースリスト */
    const struct EncodeDecodeTestForWavFileTestCase test_case[] = {
      { "unit_impulse_mono.wav", 4,  128, 5.0e-2 }, { "unit_impulse_mono.wav", 4,  256, 5.0e-2 },
      { "unit_impulse_mono.wav", 4, 1024, 5.0e-2 }, { "unit_impulse_mono.wav", 4, 4096, 5.0e-2 },
      { "unit_impulse.wav",      4,  128, 5.0e-2 }, { "unit_impulse.wav",      4,  256, 5.0e-2 },
      { "unit_impulse.wav",      4, 1024, 5.0e-2 }, { "unit_impulse.wav",      4, 4096, 5.0e-2 },
      { "sin300Hz_mono.wav",     4,  128, 5.0e-2 }, { "sin300Hz_mono.wav",     4,  256, 5.0e-2 },
      { "sin300Hz_mono.wav",     4, 1024, 5.0e-2 }, { "sin300Hz_mono.wav",     4, 4096, 5.0e-2 },
      { "sin300Hz.wav",          4,  128, 5.0e-2 }, { "sin300Hz.wav",          4,  256, 5.0e-2 },
      { "sin300Hz.wav",          4, 1024, 5.0e-2 }, { "sin300Hz.wav",          4, 4096, 5.0e-2 },
      { "bunny1.wav",            4,  128, 5.0e-2 }, { "bunny1.wav",            4,  256, 5.0e-2 },
      { "bunny1.wav",            4, 1024, 5.0e-2 }, { "bunny1.wav",            4, 4096, 5.0e-2 },
      { "pi_15-25sec.wav",       4,  128, 5.0e-2 }, { "pi_15-25sec.wav",       4,  256, 5.0e-2 },
      { "pi_15-25sec.wav",       4, 1024, 5.0e-2 }, { "pi_15-25sec.wav",       4, 4096, 5.0e-2 },
      { "unit_impulse_mono.wav", 3,  128, 6.0e-2 }, { "unit_impulse_mono.wav", 3,  256, 6.0e-2 },
      { "unit_impulse_mono.wav", 3, 1024, 6.0e-2 }, { "unit_impulse_mono.wav", 3, 4096, 6.0e-2 },
      { "unit_impulse.wav",      3,  128, 6.0e-2 }, { "unit_impulse.wav",      3,  256, 6.0e-2 },
      { "unit_impulse.wav",      3, 1024, 6.0e-2 }, { "unit_impulse.wav",      3, 4096, 6.0e-2 },
      { "sin300Hz_mono.wav",     3,  128, 6.0e-2 }, { "sin300Hz_mono.wav",     3,  256, 6.0e-2 },
      { "sin300Hz_mono.wav",     3, 1024, 6.0e-2 }, { "sin300Hz_mono.wav",     3, 4096, 6.0e-2 },
      { "sin300Hz.wav",          3,  128, 6.0e-2 }, { "sin300Hz.wav",          3,  256, 6.0e-2 },
      { "sin300Hz.wav",          3, 1024, 6.0e-2 }, { "sin300Hz.wav",          3, 4096, 6.0e-2 },
      { "bunny1.wav",            3,  128, 6.0e-2 }, { "bunny1.wav",            3,  256, 6.0e-2 },
      { "bunny1.wav",            3, 1024, 6.0e-2 }, { "bunny1.wav",            3, 4096, 6.0e-2 },
      { "pi_15-25sec.wav",       3,  128, 6.0e-2 }, { "pi_15-25sec.wav",       3,  256, 6.0e-2 },
      { "pi_15-25sec.wav",       3, 1024, 6.0e-2 }, { "pi_15-25sec.wav",       3, 4096, 6.0e-2 },
      { "unit_impulse_mono.wav", 2,  128, 8.0e-2 }, { "unit_impulse_mono.wav", 2,  256, 8.0e-2 },
      { "unit_impulse_mono.wav", 2, 1024, 8.0e-2 }, { "unit_impulse_mono.wav", 2, 4096, 8.0e-2 },
      { "unit_impulse.wav",      2,  128, 8.0e-2 }, { "unit_impulse.wav",      2,  256, 8.0e-2 },
      { "unit_impulse.wav",      2, 1024, 8.0e-2 }, { "unit_impulse.wav",      2, 4096, 8.0e-2 },
      { "sin300Hz_mono.wav",     2,  128, 8.0e-2 }, { "sin300Hz_mono.wav",     2,  256, 8.0e-2 },
      { "sin300Hz_mono.wav",     2, 1024, 8.0e-2 }, { "sin300Hz_mono.wav",     2, 4096, 8.0e-2 },
      { "sin300Hz.wav",          2,  128, 8.0e-2 }, { "sin300Hz.wav",          2,  256, 8.0e-2 },
      { "sin300Hz.wav",          2, 1024, 8.0e-2 }, { "sin300Hz.wav",          2, 4096, 8.0e-2 },
      { "bunny1.wav",            2,  128, 8.0e-2 }, { "bunny1.wav",            2,  256, 8.0e-2 },
      { "bunny1.wav",            2, 1024, 8.0e-2 }, { "bunny1.wav",            2, 4096, 8.0e-2 },
      { "pi_15-25sec.wav",       2,  128, 8.0e-2 }, { "pi_15-25sec.wav",       2,  256, 8.0e-2 },
      { "pi_15-25sec.wav",       2, 1024, 8.0e-2 }, { "pi_15-25sec.wav",       2, 4096, 8.0e-2 },
    };
    const uint32_t num_test_cases = sizeof(test_case) / sizeof(test_case[0]);

    /* 各テストケースを実行 */
    is_ok = 1;
    for (i = 0; i < num_test_cases; i++) {
      const struct EncodeDecodeTestForWavFileTestCase *pcase = &test_case[i];
      /* 1つでも失敗したら終わる */
      /* ログがうるさくなるのを防ぐため */
      if (AADTest_EncodeDecodeCheckForWavFile(pcase->filename,
            pcase->bits_per_sample, pcase->block_size, pcase->rms_epsilon) != 1) {
        fprintf(stderr,
            "Encode/Decode Test Failed for %s bps:%d block size:%d RMS epsilon:%e \n",
            pcase->filename, pcase->bits_per_sample, pcase->block_size, pcase->rms_epsilon);
        is_ok = 0;
        break;
      }
    }
    Test_AssertEqual(is_ok, 1);
  }
}

void AADTest_Setup(void)
{
  struct TestSuite *suite
    = Test_AddTestSuite("AAD Test Suite",
        NULL, AADTest_Initialize, AADTest_Finalize);

  Test_AddTest(suite, AADTest_HeaderEncodeDecodeTest);
  Test_AddTest(suite, AADTest_CalculateBlockSizeTest);
  Test_AddTest(suite, AADDecoderTest_CreateDestroyTest);
  Test_AddTest(suite, AADDecoderTest_DecodeTest);
  Test_AddTest(suite, AADEncoderTest_CreateDestroyTest);
  Test_AddTest(suite, AADEncoderTest_SetEncodeParameterTest);
  Test_AddTest(suite, AADTest_EncodeDecodeTest);
}
