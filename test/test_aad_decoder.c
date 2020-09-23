#include "test.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <assert.h>

/* テスト対象のモジュール */
#include "../aad_decoder.c"

/* テストデータ作成のためにエンコーダを使う */
#include "../aad_encoder.h"

/* リファレンス一致確認のためにwavを使用 */
#include "../wav.h"

/* テストのセットアップ関数 */
void AADDecoderTest_Setup(void);

static int AADDecoderTest_Initialize(void *obj)
{
  TEST_UNUSED_PARAMETER(obj);
  return 0;
}

static int AADDecoderTest_Finalize(void *obj)
{
  TEST_UNUSED_PARAMETER(obj);
  return 0;
}

/* ヘッダデコードテスト */
static void AADDecoderTest_HeaderDecodeTest(void *obj)
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

    /* 異常なコーデックバージョン */
    memcpy(data, valid_data, sizeof(valid_data));
    ByteArray_WriteUint32BE(&data[8], 0);
    Test_AssertEqual(AADDecoder_DecodeHeader(data, sizeof(data), &getheader), AAD_APIRESULT_INVALID_FORMAT);
    memcpy(data, valid_data, sizeof(valid_data));
    ByteArray_WriteUint32BE(&data[8], AAD_CODEC_VERSION + 1);
    Test_AssertEqual(AADDecoder_DecodeHeader(data, sizeof(data), &getheader), AAD_APIRESULT_INVALID_FORMAT);

    /* 異常なチャンネル数 */
    memcpy(data, valid_data, sizeof(valid_data));
    ByteArray_WriteUint16BE(&data[12], 0);
    Test_AssertEqual(AADDecoder_DecodeHeader(data, sizeof(data), &getheader), AAD_APIRESULT_INVALID_FORMAT);
    memcpy(data, valid_data, sizeof(valid_data));
    ByteArray_WriteUint16BE(&data[12], AAD_MAX_NUM_CHANNELS + 1);
    Test_AssertEqual(AADDecoder_DecodeHeader(data, sizeof(data), &getheader), AAD_APIRESULT_INVALID_FORMAT);

    /* 異常なサンプル数 */
    memcpy(data, valid_data, sizeof(valid_data));
    ByteArray_WriteUint32BE(&data[14], 0);
    Test_AssertEqual(AADDecoder_DecodeHeader(data, sizeof(data), &getheader), AAD_APIRESULT_INVALID_FORMAT);

    /* 異常なサンプリングレート */
    memcpy(data, valid_data, sizeof(valid_data));
    ByteArray_WriteUint32BE(&data[18], 0);
    Test_AssertEqual(AADDecoder_DecodeHeader(data, sizeof(data), &getheader), AAD_APIRESULT_INVALID_FORMAT);

    /* 異常なサンプルあたりビット数 */
    memcpy(data, valid_data, sizeof(valid_data));
    ByteArray_WriteUint16BE(&data[22], 0);
    Test_AssertEqual(AADDecoder_DecodeHeader(data, sizeof(data), &getheader), AAD_APIRESULT_INVALID_FORMAT);
    memcpy(data, valid_data, sizeof(valid_data));
    ByteArray_WriteUint16BE(&data[22], AAD_MAX_BITS_PER_SAMPLE + 1);
    Test_AssertEqual(AADDecoder_DecodeHeader(data, sizeof(data), &getheader), AAD_APIRESULT_INVALID_FORMAT);

    /* 異常なブロックサイズ */
    memcpy(data, valid_data, sizeof(valid_data));
    ByteArray_WriteUint16BE(&data[24], 0);
    Test_AssertEqual(AADDecoder_DecodeHeader(data, sizeof(data), &getheader), AAD_APIRESULT_INVALID_FORMAT);
    memcpy(data, valid_data, sizeof(valid_data));
    ByteArray_WriteUint16BE(&data[24], AAD_BLOCK_HEADER_SIZE(header.num_channels) - 1);
    Test_AssertEqual(AADDecoder_DecodeHeader(data, sizeof(data), &getheader), AAD_APIRESULT_INVALID_FORMAT);

    /* 異常なブロックあたりサンプル数 */
    memcpy(data, valid_data, sizeof(valid_data));
    ByteArray_WriteUint32BE(&data[26], 0);
    Test_AssertEqual(AADDecoder_DecodeHeader(data, sizeof(data), &getheader), AAD_APIRESULT_INVALID_FORMAT);

    /* 異常なチャンネル処理法 */
    memcpy(data, valid_data, sizeof(valid_data));
    ByteArray_WriteUint8(&data[30], AAD_CH_PROCESS_METHOD_INVALID);
    Test_AssertEqual(AADDecoder_DecodeHeader(data, sizeof(data), &getheader), AAD_APIRESULT_INVALID_FORMAT);

    /* チャンネル処理法とチャンネル数指定の組み合わせがおかしい */
    memcpy(data, valid_data, sizeof(valid_data));
    ByteArray_WriteUint16BE(&data[12], 1);
    ByteArray_WriteUint8(&data[30], AAD_CH_PROCESS_METHOD_MS);
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

static uint8_t AADDecoderTest_CheckDecode(const char *encoded_file, const char *reference_wav)
{
  FILE *fp;
  struct stat fstat;
  uint32_t ch, smpl, data_size;
  uint8_t is_ok;
  uint8_t *data;
  struct AADDecoder *decoder;
  struct AADHeaderInfo      header;
  int32_t *decoded[AAD_MAX_NUM_CHANNELS];
  struct WAVFile  *wav;

  assert((encoded_file != NULL) && (reference_wav != NULL));

  /* 入力wavの読み取り */
  wav = WAV_CreateFromFile(reference_wav);
  assert(wav != NULL);

  /* データロード */
  stat(encoded_file, &fstat);
  data_size = (uint32_t)fstat.st_size;
  data = (uint8_t *)malloc(data_size);
  fp = fopen(encoded_file, "rb");
  assert(fp != NULL);
  fread(data, sizeof(uint8_t), data_size, fp);
  fclose(fp);

  /* デコーダ作成 */
  decoder = AADDecoder_Create(NULL, 0);

  /* ヘッダ読み取り */
  if (AADDecoder_DecodeHeader(data, data_size, &header) != AAD_APIRESULT_OK) {
    free(data);
    WAV_Destroy(wav);
    AADDecoder_Destroy(decoder);
    return 0;
  }

  /* 出力バッファ領域確保 */
  for (ch = 0; ch < header.num_channels; ch++) {
    decoded[ch] = malloc(sizeof(int32_t) * header.num_samples);
  }

  /* 全データをデコード */
  if (AADDecoder_DecodeWhole(decoder, 
        data, data_size, decoded, 
        header.num_channels, header.num_samples) != AAD_APIRESULT_OK) {
    is_ok = 0;
    goto CHECK_END;
  }

  /* 一致確認 */
  is_ok = 1;
  for (ch = 0; ch < header.num_channels; ch++) {
    for (smpl = 0; smpl < header.num_samples; smpl++) {
      if (WAVFile_PCM(wav, smpl, ch) != (decoded[ch][smpl] << 16)) {
        is_ok = 0;
        goto CHECK_END;
      }
    }
  }

CHECK_END:
  /* 領域開放 */
  for (ch = 0; ch < header.num_channels; ch++) {
    free(decoded[ch]);
  }
  AADDecoder_Destroy(decoder);
  free(data);
  WAV_Destroy(wav);

  return is_ok;
}

/* デコード結果の一致確認テスト */
static void AADDecoderTest_DecodeTest(void *obj)
{
  TEST_UNUSED_PARAMETER(obj);

  {
    Test_AssertEqual(AADDecoderTest_CheckDecode("sin300Hz_mono.aad", "sin300Hz_mono_decoded.wav"), 1);
    Test_AssertEqual(AADDecoderTest_CheckDecode("sin300Hz.aad", "sin300Hz_decoded.wav"), 1);
  }
}

void AADDecoderTest_Setup(void)
{
  struct TestSuite *suite
    = Test_AddTestSuite("AAD Decoder Test Suite",
        NULL, AADDecoderTest_Initialize, AADDecoderTest_Finalize);

  Test_AddTest(suite, AADDecoderTest_HeaderDecodeTest);
  Test_AddTest(suite, AADDecoderTest_CreateDestroyTest);
  Test_AddTest(suite, AADDecoderTest_DecodeTest);
}
