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
void testAAD_Setup(void);

static int testByteArray_Initialize(void *obj)
{
  TEST_UNUSED_PARAMETER(obj);
  return 0;
}

static int testByteArray_Finalize(void *obj)
{
  TEST_UNUSED_PARAMETER(obj);
  return 0;
}

/* ヘッダエンコードデコードテスト */
static void testAAD_HeaderEncodeDecodeTest(void *obj)
{
  TEST_UNUSED_PARAMETER(obj);

  /* 有効なヘッダをセット */
#define AAD_SetValidHeader(p_header) {                          \
  struct AADHeaderInfo *header__p = p_header;                   \
  header__p->num_channels           = 1;                        \
  header__p->sampling_rate          = 44100;                    \
  header__p->bytes_per_sec          = 89422;                    \
  header__p->block_size             = 256;                      \
  header__p->bits_per_sample        = AAD_MAX_BITS_PER_SAMPLE;  \
  header__p->num_samples_per_block  = 505;                      \
  header__p->num_samples            = 1024;                     \
  header__p->header_size            = AADENCODER_HEADER_SIZE;   \
}

  /* 成功例 */
  {
    uint8_t data[AADENCODER_HEADER_SIZE] = { 0, };
    struct AADHeaderInfo header = { 0, }, tmp_header = { 0, };

    AAD_SetValidHeader(&header);

    /* エンコード->デコード */
    Test_AssertEqual(AADEncoder_EncodeHeader(&header, data, sizeof(data)), AAD_APIRESULT_OK);
    Test_AssertEqual(AADDecoder_DecodeHeader(data, sizeof(data), &tmp_header), AAD_APIRESULT_OK);

    /* デコードしたヘッダの一致確認 */
    Test_AssertEqual(header.num_channels,           tmp_header.num_channels);
    Test_AssertEqual(header.sampling_rate,          tmp_header.sampling_rate);
    Test_AssertEqual(header.bytes_per_sec,          tmp_header.bytes_per_sec);
    Test_AssertEqual(header.block_size,             tmp_header.block_size);
    Test_AssertEqual(header.bits_per_sample,        tmp_header.bits_per_sample);
    Test_AssertEqual(header.num_samples_per_block,  tmp_header.num_samples_per_block);
    Test_AssertEqual(header.num_samples,            tmp_header.num_samples);
    Test_AssertEqual(header.header_size,            tmp_header.header_size);
  }

  /* ヘッダエンコード失敗ケース */
  {
    struct AADHeaderInfo header;
    uint8_t data[AADENCODER_HEADER_SIZE] = { 0, };

    /* 引数が不正 */
    AAD_SetValidHeader(&header);
    Test_AssertEqual(AADEncoder_EncodeHeader(NULL, data, sizeof(data)), AAD_APIRESULT_INVALID_ARGUMENT);
    Test_AssertEqual(AADEncoder_EncodeHeader(&header, NULL, sizeof(data)), AAD_APIRESULT_INVALID_ARGUMENT);

    /* データサイズ不足 */
    AAD_SetValidHeader(&header);
    Test_AssertEqual(AADEncoder_EncodeHeader(&header, data, sizeof(data) - 1), AAD_APIRESULT_INSUFFICIENT_DATA);
    Test_AssertEqual(AADEncoder_EncodeHeader(&header, data, AADENCODER_HEADER_SIZE - 1), AAD_APIRESULT_INSUFFICIENT_DATA);

    /* チャンネル数異常 */
    AAD_SetValidHeader(&header);
    header.num_channels = 3;
    Test_AssertEqual(AADEncoder_EncodeHeader(&header, data, sizeof(data)), AAD_APIRESULT_INVALID_FORMAT);

    /* ビット深度異常（0） */
    AAD_SetValidHeader(&header);
    header.bits_per_sample = 0;
    Test_AssertEqual(AADEncoder_EncodeHeader(&header, data, sizeof(data)), AAD_APIRESULT_INVALID_FORMAT);

    /* ビット深度異常（大きい） */
    AAD_SetValidHeader(&header);
    header.bits_per_sample = AAD_MAX_BITS_PER_SAMPLE + 1;
    Test_AssertEqual(AADEncoder_EncodeHeader(&header, data, sizeof(data)), AAD_APIRESULT_INVALID_FORMAT);
  }

  /* ヘッダデコード失敗ケース */
  {
    struct AADHeaderInfo header, getheader;
    uint8_t valid_data[AADENCODER_HEADER_SIZE] = { 0, };
    uint8_t data[AADENCODER_HEADER_SIZE];

    /* 有効な内容を作っておく */
    AAD_SetValidHeader(&header);
    AADEncoder_EncodeHeader(&header, valid_data, sizeof(valid_data));

    /* チャンクIDが不正 */
    /* RIFFの破壊 */
    memcpy(data, valid_data, sizeof(valid_data));
    data[0] = 'a';
    Test_AssertEqual(AADDecoder_DecodeHeader(data, sizeof(data), &getheader), AAD_APIRESULT_INVALID_FORMAT);
    /* WAVEの破壊 */
    memcpy(data, valid_data, sizeof(valid_data));
    data[8] = 'a';
    Test_AssertEqual(AADDecoder_DecodeHeader(data, sizeof(data), &getheader), AAD_APIRESULT_INVALID_FORMAT);
    /* FMTの破壊 */
    memcpy(data, valid_data, sizeof(valid_data));
    data[12] = 'a';
    Test_AssertEqual(AADDecoder_DecodeHeader(data, sizeof(data), &getheader), AAD_APIRESULT_INVALID_FORMAT);
    /* factの破壊はスキップ: factチャンクはオプショナルだから
    memcpy(data, valid_data, sizeof(valid_data));
    data[40] = 'a';
    Test_AssertEqual(AADDecoder_DecodeHeader(data, sizeof(data), &getheader), AAD_APIRESULT_INVALID_FORMAT);
    */
    /* dataの破壊: dataチャンクが見つけられない */
    memcpy(data, valid_data, sizeof(valid_data));
    data[52] = 'a';
    Test_AssertEqual(AADDecoder_DecodeHeader(data, sizeof(data), &getheader), AAD_APIRESULT_INSUFFICIENT_DATA);

    /* クソデカfmtチャンクサイズ */
    memcpy(data, valid_data, sizeof(valid_data));
    ByteArray_WriteUint32LE(&data[16], sizeof(data));
    Test_AssertEqual(AADDecoder_DecodeHeader(data, sizeof(data), &getheader), AAD_APIRESULT_INSUFFICIENT_DATA);
    /* 異常なWAVEフォーマットタイプ */
    memcpy(data, valid_data, sizeof(valid_data));
    ByteArray_WriteUint32LE(&data[20], 0);
    Test_AssertEqual(AADDecoder_DecodeHeader(data, sizeof(data), &getheader), AAD_APIRESULT_INVALID_FORMAT);
    /* 異常なチャンネル数 */
    memcpy(data, valid_data, sizeof(valid_data));
    ByteArray_WriteUint32LE(&data[22], AAD_MAX_NUM_CHANNELS + 1);
    Test_AssertEqual(AADDecoder_DecodeHeader(data, sizeof(data), &getheader), AAD_APIRESULT_INVALID_FORMAT);
    /* 異常なfmtチャンクのエキストラサイズ */
    memcpy(data, valid_data, sizeof(valid_data));
    ByteArray_WriteUint32LE(&data[36], 0);
    Test_AssertEqual(AADDecoder_DecodeHeader(data, sizeof(data), &getheader), AAD_APIRESULT_INVALID_FORMAT);
    /* 異常なFACTチャンクサイズ */
    memcpy(data, valid_data, sizeof(valid_data));
    ByteArray_WriteUint32LE(&data[44], 0);
    Test_AssertEqual(AADDecoder_DecodeHeader(data, sizeof(data), &getheader), AAD_APIRESULT_INVALID_FORMAT);
  }
}

/* デコードハンドル作成破棄テスト */
static void testAADDecoder_CreateDestroyTest(void *obj)
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
    Test_AssertCondition(decoder->work == NULL);

    AADDecoder_Destroy(decoder);
    free(work);
  }

  /* 自前確保によるハンドル作成（成功例） */
  {
    struct AADDecoder *decoder;

    decoder = AADDecoder_Create(NULL, 0);
    Test_AssertCondition(decoder != NULL);
    Test_AssertCondition(decoder->work != NULL);

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

/* デコード結果が一致するか確認するサブルーチン 一致していたら1, していなければ0を返す */
static uint8_t testAADDecoder_CheckDecodeResult(const char *adpcm_filename, const char *decodedwav_filename)
{
  FILE        *fp;
  uint8_t     *data;
  struct stat fstat;
  uint32_t    data_size;
  uint32_t    ch, smpl, is_ok;
  int32_t     *output[AAD_MAX_NUM_CHANNELS];
  struct AADDecoder *decoder;
  struct AADHeaderInfo header;
  struct WAVFile *wavfile;

  /* データロード */
  fp = fopen(adpcm_filename, "rb");
  assert(fp != NULL);
  stat(adpcm_filename, &fstat);
  data_size = (uint32_t)fstat.st_size;
  data = (uint8_t *)malloc(data_size);
  fread(data, sizeof(uint8_t), data_size, fp);
  fclose(fp);

  /* ヘッダデコード */
  if (AADDecoder_DecodeHeader(data, data_size, &header) != AAD_APIRESULT_OK) {
    free(data);
    return 0;
  }

  /* リソース確保 */
  wavfile = WAV_CreateFromFile(decodedwav_filename);
  assert(wavfile != NULL);
  for (ch = 0; ch < header.num_channels; ch++) {
    output[ch] = malloc(sizeof(int32_t) * header.num_samples);
  }
  decoder = AADDecoder_Create(NULL, 0);

  /* デコード実行 */
  if (AADDecoder_DecodeWhole(decoder,
        data, data_size, output, header.num_channels, header.num_samples) != AAD_APIRESULT_OK) {
    is_ok = 0;
    goto CHECK_END;
  }

  /* リファレンスと一致するか？ */
  is_ok = 1;
  for (ch = 0; ch < header.num_channels; ch++) {
    for (smpl = 0; smpl < header.num_samples; smpl++) {
      if (WAVFile_PCM(wavfile, smpl, ch) != (output[ch][smpl] << 16)) {
        is_ok = 0;
        goto CHECK_END;
      }
    }
  }

CHECK_END:
  /* 確保した領域の開放 */
  WAV_Destroy(wavfile);
  AADDecoder_Destroy(decoder);
  for (ch = 0; ch < header.num_channels; ch++) {
    free(output[ch]);
  }
  free(data);
  
  return is_ok;
}

/* デコードテスト */
static void testAADDecoder_DecodeTest(void *obj)
{
  TEST_UNUSED_PARAMETER(obj);

#if 0
  /* 実データのヘッダデコード */
  {
    const char  test_filename[] = "sin300Hz_mono_adpcm_ffmpeg.wav";
    FILE        *fp;
    uint8_t     *data;
    struct stat fstat;
    uint32_t    data_size;
    struct AADHeaderInfo header;

    /* データロード */
    fp = fopen(test_filename, "rb");
    assert(fp != NULL);
    stat(test_filename, &fstat);
    data_size = (uint32_t)fstat.st_size;
    data = (uint8_t *)malloc(data_size);
    fread(data, sizeof(uint8_t), data_size, fp);
    fclose(fp);

    /* ヘッダデコード */
    Test_AssertEqual(AADDecoder_DecodeHeader(data, data_size, &header), AAD_APIRESULT_OK);

    /* 想定した内容になっているか */
    Test_AssertEqual(header.num_channels,           1    );
    Test_AssertEqual(header.sampling_rate,          48000);
    Test_AssertEqual(header.bytes_per_sec,          16000);
    Test_AssertEqual(header.block_size,             1024 );
    Test_AssertEqual(header.num_samples_per_block,  2041 );
    Test_AssertEqual(header.num_samples,            24492);
    Test_AssertEqual(header.header_size,            94   );

    free(data);
  }

  /* 実データのヘッダデコード（factチャンクがないwav） */
  {
    const char  test_filename[] = "bunny1im.wav";
    FILE        *fp;
    uint8_t     *data;
    struct stat fstat;
    uint32_t    data_size;
    struct AADHeaderInfo header;

    /* データロード */
    fp = fopen(test_filename, "rb");
    assert(fp != NULL);
    stat(test_filename, &fstat);
    data_size = (uint32_t)fstat.st_size;
    data = (uint8_t *)malloc(data_size);
    fread(data, sizeof(uint8_t), data_size, fp);
    fclose(fp);

    /* ヘッダデコード */
    Test_AssertEqual(AADDecoder_DecodeHeader(data, data_size, &header), AAD_APIRESULT_OK);

    /* 想定した内容になっているか */
    Test_AssertEqual(header.num_channels,           1     );
    Test_AssertEqual(header.sampling_rate,          8000  );
    Test_AssertEqual(header.bytes_per_sec,          4064  );
    Test_AssertEqual(header.block_size,             256   );
    Test_AssertEqual(header.num_samples_per_block,  505   );
    Test_AssertEqual(header.num_samples,            187860);
    Test_AssertEqual(header.header_size,            48    );

    free(data);
  }
#endif

  /* 実データのデータデコード一致確認 */
#if 0
  {
    Test_AssertEqual(
        testAADDecoder_CheckDecodeResult(
          "sin300Hz_mono_adpcm_ffmpeg.wav", "sin300Hz_mono_adpcm_ffmpeg_decoded.wav"), 1);
    Test_AssertEqual(
        testAADDecoder_CheckDecodeResult(
          "sin300Hz_adpcm_ffmpeg.wav", "sin300Hz_adpcm_ffmpeg_decoded.wav"), 1);
    Test_AssertEqual(
        testAADDecoder_CheckDecodeResult(
          "unit_impulse_mono_adpcm_ffmpeg.wav", "unit_impulse_mono_adpcm_ffmpeg_decoded.wav"), 1);
    Test_AssertEqual(
        testAADDecoder_CheckDecodeResult(
          "unit_impulse_adpcm_ffmpeg.wav", "unit_impulse_adpcm_ffmpeg_decoded.wav"), 1);
  }
#endif
}

/* エンコードハンドル作成破棄テスト */
static void testAADEncoder_CreateDestroyTest(void *obj)
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
    Test_AssertCondition(encoder->work == NULL);
    Test_AssertCondition(encoder->set_parameter == 0);

    AADEncoder_Destroy(encoder);
    free(work);
  }

  /* 自前確保によるハンドル作成（成功例） */
  {
    struct AADEncoder *encoder;

    encoder = AADEncoder_Create(NULL, 0);
    Test_AssertCondition(encoder != NULL);
    Test_AssertCondition(encoder->work != NULL);

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
static void testAADEncoder_SetEncodeParameterTest(void *obj)
{
  TEST_UNUSED_PARAMETER(obj);

  /* 有効なパラメータをセット */
#define AAD_SetValidParameter(p_param) {                  \
    struct AADEncodeParameter *p__param = p_param;        \
    p__param->num_channels    = 1;                        \
    p__param->sampling_rate   = 8000;                     \
    p__param->bits_per_sample = AAD_MAX_BITS_PER_SAMPLE;  \
    p__param->block_size      = 256;                      \
}

  /* 成功例 */
  {
    struct AADEncoder *encoder;
    struct AADEncodeParameter param;

    encoder = AADEncoder_Create(NULL, 0);
    
    AAD_SetValidParameter(&param);
    Test_AssertEqual(AADEncoder_SetEncodeParameter(encoder, &param), AAD_APIRESULT_OK);
    Test_AssertEqual(encoder->set_parameter, 1);
    Test_AssertEqual(memcmp(&(encoder->encode_paramemter), &param, sizeof(struct AADEncodeParameter)), 0);

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

    /* ブロックサイズが小さすぎる */
    AAD_SetValidParameter(&param);
    param.block_size = 0;
    Test_AssertEqual(AADEncoder_SetEncodeParameter(encoder, &param), AAD_APIRESULT_INVALID_FORMAT);
    AAD_SetValidParameter(&param);
    param.block_size = param.num_channels * 4;
    Test_AssertEqual(AADEncoder_SetEncodeParameter(encoder, &param), AAD_APIRESULT_INVALID_FORMAT);

    AADEncoder_Destroy(encoder);
  }
}

/* エンコード→デコードテスト 成功時は1, 失敗時は0を返す */
static uint8_t testAADEncoder_EncodeDecodeTest(
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

  /* 出力データの領域割当て */
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
  enc_param.block_size      = block_size;
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
  if (rms_error < rms_epsilon) {
    is_ok = 1;
  } else {
    is_ok = 0;
  }

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

/* エンコードテスト */
static void testAADDecoder_EncodeTest(void *obj)
{
  TEST_UNUSED_PARAMETER(obj);

  /* 簡単なデータをエンコード→デコードしてみる */
  {
#define NUM_CHANNELS  1
#define NUM_SAMPLES   2048
    int32_t *input[AAD_MAX_NUM_CHANNELS];
    int32_t *decoded[AAD_MAX_NUM_CHANNELS];
    uint32_t ch, smpl, buffer_size, output_size;
    uint8_t *buffer;
    double rms_error;
    struct AADEncodeParameter enc_param;
    struct AADEncoder *encoder;
    struct AADDecoder *decoder;

    /* 出力データの領域割当て */
    for (ch = 0; ch < NUM_CHANNELS; ch++) {
      input[ch] = malloc(sizeof(int32_t) * NUM_SAMPLES);
      decoded[ch] = malloc(sizeof(int32_t) * NUM_SAMPLES);
    }
    /* 入力wavと同じサイズの出力領域を確保（増えることはないと期待） */
    buffer_size = NUM_CHANNELS * NUM_SAMPLES * sizeof(int32_t);
    buffer = malloc(buffer_size);

    /* データ作成: 正弦波 */
    for (ch = 0; ch < NUM_CHANNELS; ch++) {
      for (smpl = 0; smpl < NUM_SAMPLES; smpl++) {
        input[ch][smpl] = INT16_MAX * 0.5 * sin((2.0 * 3.1415 * 440.0 * smpl) / 48000.0);
      }
    }

    /* ハンドル作成 */
    encoder = AADEncoder_Create(NULL, 0);
    decoder = AADDecoder_Create(NULL, 0);

    /* エンコードパラメータをセット */
    enc_param.num_channels    = NUM_CHANNELS;
    enc_param.sampling_rate   = 8000;
    enc_param.bits_per_sample = AAD_MAX_BITS_PER_SAMPLE;
    enc_param.block_size      = 256;
    Test_AssertEqual(AADEncoder_SetEncodeParameter(encoder, &enc_param), AAD_APIRESULT_OK);

    /* エンコード */
    Test_AssertEqual(
        AADEncoder_EncodeWhole(
          encoder, (const int32_t *const *)input, NUM_SAMPLES,
          buffer, buffer_size, &output_size), AAD_APIRESULT_OK);
    /* 半分以下にはなるはず */
    Test_AssertCondition(output_size < (buffer_size / 2));

    /* デコード */
    Test_AssertEqual(
        AADDecoder_DecodeWhole(
          decoder, buffer, output_size,
          decoded, NUM_CHANNELS, NUM_SAMPLES), AAD_APIRESULT_OK);

    /* ロスがあるのでRMSE基準でチェック */
    rms_error = 0.0;
    for (ch = 0; ch < NUM_CHANNELS; ch++) {
      for (smpl = 0; smpl < NUM_SAMPLES; smpl++) {
        double pcm1, pcm2, abs_error;
        pcm1 = (double)input[ch][smpl] / INT16_MAX;
        pcm2 = (double)decoded[ch][smpl] / INT16_MAX;
        abs_error = fabs(pcm1 - pcm2);
        rms_error += abs_error * abs_error;
      }
    }
    rms_error = sqrt(rms_error / (NUM_SAMPLES * NUM_CHANNELS));

    /* 経験的に0.05 */
    Test_AssertCondition(rms_error < 5.0e-2);

    /* 領域開放 */
    AADEncoder_Destroy(encoder);
    AADDecoder_Destroy(decoder);
    free(buffer);
    for (ch = 0; ch < NUM_CHANNELS; ch++) {
      free(input[ch]);
      free(decoded[ch]);
    }
#undef NUM_CHANNELS
#undef NUM_SAMPLES
  }

  /* エンコードデコードテスト */
  {
    Test_AssertEqual(testAADEncoder_EncodeDecodeTest("unit_impulse_mono.wav", 4,  128, 5.0e-2), 1);
    Test_AssertEqual(testAADEncoder_EncodeDecodeTest("unit_impulse_mono.wav", 4,  256, 5.0e-2), 1);
    Test_AssertEqual(testAADEncoder_EncodeDecodeTest("unit_impulse_mono.wav", 4,  512, 5.0e-2), 1);
    Test_AssertEqual(testAADEncoder_EncodeDecodeTest("unit_impulse_mono.wav", 4, 1024, 5.0e-2), 1);
    Test_AssertEqual(testAADEncoder_EncodeDecodeTest("unit_impulse.wav",      4,  128, 5.0e-2), 1);
    Test_AssertEqual(testAADEncoder_EncodeDecodeTest("unit_impulse.wav",      4,  256, 5.0e-2), 1);
    Test_AssertEqual(testAADEncoder_EncodeDecodeTest("unit_impulse.wav",      4,  512, 5.0e-2), 1);
    Test_AssertEqual(testAADEncoder_EncodeDecodeTest("unit_impulse.wav",      4, 1024, 5.0e-2), 1);
    Test_AssertEqual(testAADEncoder_EncodeDecodeTest("sin300Hz_mono.wav",     4,  128, 5.0e-2), 1);
    Test_AssertEqual(testAADEncoder_EncodeDecodeTest("sin300Hz_mono.wav",     4,  256, 5.0e-2), 1);
    Test_AssertEqual(testAADEncoder_EncodeDecodeTest("sin300Hz_mono.wav",     4,  512, 5.0e-2), 1);
    Test_AssertEqual(testAADEncoder_EncodeDecodeTest("sin300Hz_mono.wav",     4, 1024, 5.0e-2), 1);
    Test_AssertEqual(testAADEncoder_EncodeDecodeTest("sin300Hz.wav",          4,  128, 5.0e-2), 1);
    Test_AssertEqual(testAADEncoder_EncodeDecodeTest("sin300Hz.wav",          4,  256, 5.0e-2), 1);
    Test_AssertEqual(testAADEncoder_EncodeDecodeTest("sin300Hz.wav",          4,  512, 5.0e-2), 1);
    Test_AssertEqual(testAADEncoder_EncodeDecodeTest("sin300Hz.wav",          4, 1024, 5.0e-2), 1);
  }

}

void testAAD_Setup(void)
{
  struct TestSuite *suite
    = Test_AddTestSuite("AAD Test Suite",
        NULL, testByteArray_Initialize, testByteArray_Finalize);

  Test_AddTest(suite, testAAD_HeaderEncodeDecodeTest);
  Test_AddTest(suite, testAADDecoder_CreateDestroyTest);
  Test_AddTest(suite, testAADDecoder_DecodeTest);
  Test_AddTest(suite, testAADEncoder_CreateDestroyTest);
  Test_AddTest(suite, testAADEncoder_SetEncodeParameterTest);
  Test_AddTest(suite, testAADDecoder_EncodeTest);
}
