#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <assert.h>
#include <math.h>
#include "test.h"

/* テスト対象のモジュール */
#include "../aad_encoder.h"
#include "../aad_decoder.h"

/* 追加でwavを使う */
#include "../wav.c"

/* テストのセットアップ関数 */
void AADEncodeDecodeTest_Setup(void);

static int AADEncodeDecodeTest_Initialize(void *obj)
{
  TEST_UNUSED_PARAMETER(obj);
  return 0;
}

static int AADEncodeDecodeTest_Finalize(void *obj)
{
  TEST_UNUSED_PARAMETER(obj);
  return 0;
}

/* ヘッダエンコードデコードテスト */
static void AADEncodeDecodeTest_EncodeDecodeHeaderTest(void *obj)
{
  TEST_UNUSED_PARAMETER(obj);

  /* 有効なヘッダをセット */
#define AAD_SetValidHeader(p_header) {                            \
  struct AADHeaderInfo *header__p = p_header;                     \
  header__p->num_channels           = 1;                          \
  header__p->sampling_rate          = 44100;                      \
  header__p->block_size             = 32;                         \
  header__p->bits_per_sample        = AAD_MAX_BITS_PER_SAMPLE;    \
  header__p->num_samples            = 1024;                       \
  header__p->num_samples_per_block  = 32;                         \
  header__p->ch_process_method      = AAD_CH_PROCESS_METHOD_NONE; \
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
    Test_AssertEqual(header.ch_process_method,      tmp_header.ch_process_method);
  }

}

/* 与えられたPCMデータに対するエンコードデコードテスト 成功時は1を、失敗時は0を返す */
static uint8_t AADEncodeDecodeTest_EncodeDecodeCheckForPcmData(
    int32_t **input, 
    uint32_t num_channels, uint32_t num_samples,
    uint16_t bits_per_sample, uint16_t block_size, AADChannelProcessMethod ch_process_method,
    uint8_t num_encode_trials,
    double rms_epsilon)
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
  encoder = AADEncoder_Create(block_size, NULL, 0);
  decoder = AADDecoder_Create(NULL, 0);

  /* エンコードパラメータをセット */
  enc_param.num_channels        = num_channels;
  enc_param.sampling_rate       = 8000;
  enc_param.bits_per_sample     = bits_per_sample;
  enc_param.max_block_size      = block_size;
  enc_param.ch_process_method   = ch_process_method;
  enc_param.num_encode_trials   = num_encode_trials;
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
    printf("%f \n", rms_error);
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
static uint8_t AADEncodeDecodeTest_EncodeDecodeCheckForWavFile(
    const char *wav_filename, 
    uint16_t bits_per_sample, uint16_t block_size, AADChannelProcessMethod ch_process_method,
    uint8_t num_encode_trials,
    double rms_epsilon)
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
  encoder = AADEncoder_Create(block_size, NULL, 0);
  decoder = AADDecoder_Create(NULL, 0);

  /* エンコードパラメータをセット */
  enc_param.num_channels      = num_channels;
  enc_param.sampling_rate     = wavfile->format.sampling_rate;
  enc_param.bits_per_sample   = bits_per_sample;
  enc_param.max_block_size    = block_size;
  enc_param.ch_process_method = ch_process_method;
  enc_param.num_encode_trials = num_encode_trials;
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
static void AADEncodeDecodeTest_EncodeDecodeTest(void *obj)
{
  TEST_UNUSED_PARAMETER(obj);

  /* PCMデータをエンコード->デコードしてみて、誤差が許容範囲内に収まるか？ */
  {
#define MAX_NUM_CHANNELS  AAD_MAX_NUM_CHANNELS
#define MAX_NUM_SAMPLES   2048
    /* テストケース */
    /* FIXME: エンコードパラメータを直接含めよ */
    struct EncodeDecodeTestForPcmDataTestCase {
      uint32_t                num_channels;
      uint32_t                num_samples;
      uint16_t                bits_per_sample;
      uint16_t                block_size;
      AADChannelProcessMethod ch_process_method;
      uint8_t                 num_encode_trials;
      double                  rms_epsilon;
    };
    int32_t *input[MAX_NUM_CHANNELS];
    uint32_t ch, smpl, i;
    uint8_t is_ok;

    /* 正弦波向けテストケースリスト */
    const struct EncodeDecodeTestForPcmDataTestCase test_case_for_sin[] = {
      { 1, MAX_NUM_SAMPLES, 4,  128, AAD_CH_PROCESS_METHOD_NONE, 0, 5.0e-2 },
      { 2, MAX_NUM_SAMPLES, 4,  128, AAD_CH_PROCESS_METHOD_NONE, 0, 5.0e-2 },
      { 2, MAX_NUM_SAMPLES, 4,  128, AAD_CH_PROCESS_METHOD_MS,   0, 5.0e-2 },
      { 1, MAX_NUM_SAMPLES, 4, 1024, AAD_CH_PROCESS_METHOD_NONE, 0, 5.0e-2 },
      { 2, MAX_NUM_SAMPLES, 4, 1024, AAD_CH_PROCESS_METHOD_NONE, 0, 5.0e-2 },
      { 2, MAX_NUM_SAMPLES, 4, 1024, AAD_CH_PROCESS_METHOD_MS,   0, 5.0e-2 },
      { 1, MAX_NUM_SAMPLES, 3,  128, AAD_CH_PROCESS_METHOD_NONE, 0, 6.0e-2 },
      { 2, MAX_NUM_SAMPLES, 3,  128, AAD_CH_PROCESS_METHOD_NONE, 0, 6.0e-2 },
      { 2, MAX_NUM_SAMPLES, 3,  128, AAD_CH_PROCESS_METHOD_MS,   0, 6.0e-2 },
      { 1, MAX_NUM_SAMPLES, 3, 1024, AAD_CH_PROCESS_METHOD_NONE, 0, 6.0e-2 },
      { 2, MAX_NUM_SAMPLES, 3, 1024, AAD_CH_PROCESS_METHOD_NONE, 0, 6.0e-2 },
      { 2, MAX_NUM_SAMPLES, 3, 1024, AAD_CH_PROCESS_METHOD_MS,   0, 6.0e-2 },
      { 1, MAX_NUM_SAMPLES, 2,  128, AAD_CH_PROCESS_METHOD_NONE, 0, 8.0e-2 },
      { 2, MAX_NUM_SAMPLES, 2,  128, AAD_CH_PROCESS_METHOD_NONE, 0, 8.0e-2 },
      { 2, MAX_NUM_SAMPLES, 2,  128, AAD_CH_PROCESS_METHOD_MS,   0, 8.0e-2 },
      { 1, MAX_NUM_SAMPLES, 2, 1024, AAD_CH_PROCESS_METHOD_NONE, 0, 8.0e-2 },
      { 2, MAX_NUM_SAMPLES, 2, 1024, AAD_CH_PROCESS_METHOD_NONE, 0, 8.0e-2 },
      { 2, MAX_NUM_SAMPLES, 2, 1024, AAD_CH_PROCESS_METHOD_MS,   0, 8.0e-2 },

      { 1, MAX_NUM_SAMPLES, 4,  128, AAD_CH_PROCESS_METHOD_NONE, 1, 5.0e-2 },
      { 2, MAX_NUM_SAMPLES, 4,  128, AAD_CH_PROCESS_METHOD_NONE, 1, 5.0e-2 },
      { 2, MAX_NUM_SAMPLES, 4,  128, AAD_CH_PROCESS_METHOD_MS,   1, 5.0e-2 },
      { 1, MAX_NUM_SAMPLES, 4, 1024, AAD_CH_PROCESS_METHOD_NONE, 1, 5.0e-2 },
      { 2, MAX_NUM_SAMPLES, 4, 1024, AAD_CH_PROCESS_METHOD_NONE, 1, 5.0e-2 },
      { 2, MAX_NUM_SAMPLES, 4, 1024, AAD_CH_PROCESS_METHOD_MS,   1, 5.0e-2 },
      { 1, MAX_NUM_SAMPLES, 3,  128, AAD_CH_PROCESS_METHOD_NONE, 1, 6.0e-2 },
      { 2, MAX_NUM_SAMPLES, 3,  128, AAD_CH_PROCESS_METHOD_NONE, 1, 6.0e-2 },
      { 2, MAX_NUM_SAMPLES, 3,  128, AAD_CH_PROCESS_METHOD_MS,   1, 6.0e-2 },
      { 1, MAX_NUM_SAMPLES, 3, 1024, AAD_CH_PROCESS_METHOD_NONE, 1, 6.0e-2 },
      { 2, MAX_NUM_SAMPLES, 3, 1024, AAD_CH_PROCESS_METHOD_NONE, 1, 6.0e-2 },
      { 2, MAX_NUM_SAMPLES, 3, 1024, AAD_CH_PROCESS_METHOD_MS,   1, 6.0e-2 },
      { 1, MAX_NUM_SAMPLES, 2,  128, AAD_CH_PROCESS_METHOD_NONE, 1, 8.0e-2 },
      { 2, MAX_NUM_SAMPLES, 2,  128, AAD_CH_PROCESS_METHOD_NONE, 1, 8.0e-2 },
      { 2, MAX_NUM_SAMPLES, 2,  128, AAD_CH_PROCESS_METHOD_MS,   1, 8.0e-2 },
      { 1, MAX_NUM_SAMPLES, 2, 1024, AAD_CH_PROCESS_METHOD_NONE, 1, 8.0e-2 },
      { 2, MAX_NUM_SAMPLES, 2, 1024, AAD_CH_PROCESS_METHOD_NONE, 1, 8.0e-2 },
      { 2, MAX_NUM_SAMPLES, 2, 1024, AAD_CH_PROCESS_METHOD_MS,   1, 8.0e-2 },
    };

    /* 白色雑音向けテストケースリスト */
    const struct EncodeDecodeTestForPcmDataTestCase test_case_for_white_noise[] = {
      { 1, MAX_NUM_SAMPLES, 4,  128, AAD_CH_PROCESS_METHOD_NONE, 0, 1.0e-1 },
      { 2, MAX_NUM_SAMPLES, 4,  128, AAD_CH_PROCESS_METHOD_NONE, 0, 1.0e-1 },
      { 2, MAX_NUM_SAMPLES, 4,  128, AAD_CH_PROCESS_METHOD_MS,   0, 1.0e-1 },
      { 1, MAX_NUM_SAMPLES, 4, 1024, AAD_CH_PROCESS_METHOD_NONE, 0, 1.0e-1 },
      { 2, MAX_NUM_SAMPLES, 4, 1024, AAD_CH_PROCESS_METHOD_NONE, 0, 1.0e-1 },
      { 2, MAX_NUM_SAMPLES, 4, 1024, AAD_CH_PROCESS_METHOD_MS,   0, 1.0e-1 },
      { 1, MAX_NUM_SAMPLES, 3,  128, AAD_CH_PROCESS_METHOD_NONE, 0, 1.5e-1 },
      { 2, MAX_NUM_SAMPLES, 3,  128, AAD_CH_PROCESS_METHOD_NONE, 0, 1.5e-1 },
      { 2, MAX_NUM_SAMPLES, 3,  128, AAD_CH_PROCESS_METHOD_MS,   0, 1.5e-1 },
      { 1, MAX_NUM_SAMPLES, 3, 1024, AAD_CH_PROCESS_METHOD_NONE, 0, 1.5e-1 },
      { 2, MAX_NUM_SAMPLES, 3, 1024, AAD_CH_PROCESS_METHOD_NONE, 0, 1.5e-1 },
      { 2, MAX_NUM_SAMPLES, 3, 1024, AAD_CH_PROCESS_METHOD_MS,   0, 1.5e-1 },
      { 1, MAX_NUM_SAMPLES, 2,  128, AAD_CH_PROCESS_METHOD_NONE, 0, 2.4e-1 },
      { 2, MAX_NUM_SAMPLES, 2,  128, AAD_CH_PROCESS_METHOD_NONE, 0, 2.4e-1 },
      { 2, MAX_NUM_SAMPLES, 2,  128, AAD_CH_PROCESS_METHOD_MS,   0, 2.4e-1 },
      { 1, MAX_NUM_SAMPLES, 2, 1024, AAD_CH_PROCESS_METHOD_NONE, 0, 2.4e-1 },
      { 2, MAX_NUM_SAMPLES, 2, 1024, AAD_CH_PROCESS_METHOD_NONE, 0, 2.4e-1 },
      { 2, MAX_NUM_SAMPLES, 2, 1024, AAD_CH_PROCESS_METHOD_MS,   0, 2.4e-1 },
    };

    /* ナイキスト振動波向けテストケースリスト */
    const struct EncodeDecodeTestForPcmDataTestCase test_case_for_nyquist[] = {
      { 1, MAX_NUM_SAMPLES, 4,  128, AAD_CH_PROCESS_METHOD_NONE, 0, 1.2e-1 },
      { 2, MAX_NUM_SAMPLES, 4,  128, AAD_CH_PROCESS_METHOD_NONE, 0, 1.2e-1 },
      { 2, MAX_NUM_SAMPLES, 4,  128, AAD_CH_PROCESS_METHOD_MS,   0, 1.2e-1 },
      { 1, MAX_NUM_SAMPLES, 4, 1024, AAD_CH_PROCESS_METHOD_NONE, 0, 1.2e-1 },
      { 2, MAX_NUM_SAMPLES, 4, 1024, AAD_CH_PROCESS_METHOD_NONE, 0, 1.2e-1 },
      { 2, MAX_NUM_SAMPLES, 4, 1024, AAD_CH_PROCESS_METHOD_MS,   0, 1.2e-1 },
      { 1, MAX_NUM_SAMPLES, 3,  128, AAD_CH_PROCESS_METHOD_NONE, 0, 1.6e-1 },
      { 2, MAX_NUM_SAMPLES, 3,  128, AAD_CH_PROCESS_METHOD_NONE, 0, 1.6e-1 },
      { 2, MAX_NUM_SAMPLES, 3,  128, AAD_CH_PROCESS_METHOD_MS,   0, 1.6e-1 },
      { 1, MAX_NUM_SAMPLES, 3, 1024, AAD_CH_PROCESS_METHOD_NONE, 0, 1.6e-1 },
      { 2, MAX_NUM_SAMPLES, 3, 1024, AAD_CH_PROCESS_METHOD_NONE, 0, 1.6e-1 },
      { 2, MAX_NUM_SAMPLES, 3, 1024, AAD_CH_PROCESS_METHOD_MS,   0, 1.6e-1 },
      { 1, MAX_NUM_SAMPLES, 2,  128, AAD_CH_PROCESS_METHOD_NONE, 0, 2.3e-1 },
      { 2, MAX_NUM_SAMPLES, 2,  128, AAD_CH_PROCESS_METHOD_NONE, 0, 2.3e-1 },
      { 2, MAX_NUM_SAMPLES, 2,  128, AAD_CH_PROCESS_METHOD_MS,   0, 2.3e-1 },
      { 1, MAX_NUM_SAMPLES, 2, 1024, AAD_CH_PROCESS_METHOD_NONE, 0, 2.3e-1 },
      { 2, MAX_NUM_SAMPLES, 2, 1024, AAD_CH_PROCESS_METHOD_NONE, 0, 2.3e-1 },
      { 2, MAX_NUM_SAMPLES, 2, 1024, AAD_CH_PROCESS_METHOD_MS,   0, 2.3e-1 },
    };

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
    
    /* 定義済みテストケースに対してテスト */
    is_ok = 1;
    for (i = 0; i < sizeof(test_case_for_sin) / sizeof(test_case_for_sin[0]); i++) {
      const struct EncodeDecodeTestForPcmDataTestCase *pcase = &test_case_for_sin[i];
      if (AADEncodeDecodeTest_EncodeDecodeCheckForPcmData(input, 
            pcase->num_channels, pcase->num_samples, pcase->bits_per_sample,
            pcase->block_size, pcase->ch_process_method, pcase->num_encode_trials,
            pcase->rms_epsilon) != 1) {
        is_ok = 0;
        break;
      }
    }
    Test_AssertEqual(is_ok, 1);

    /* 白色雑音作成 */
    srand(0);
    for (ch = 0; ch < MAX_NUM_CHANNELS; ch++) {
      for (smpl = 0; smpl < MAX_NUM_SAMPLES; smpl++) {
        input[ch][smpl] = INT16_MAX * 2.0f * ((double)rand() / RAND_MAX - 0.5f);
      }
    }
    
    /* 定義済みテストケースに対してテスト */
    is_ok = 1;
    for (i = 0; i < sizeof(test_case_for_white_noise) / sizeof(test_case_for_white_noise[0]); i++) {
      const struct EncodeDecodeTestForPcmDataTestCase *pcase = &test_case_for_white_noise[i];
      if (AADEncodeDecodeTest_EncodeDecodeCheckForPcmData(input, 
            pcase->num_channels, pcase->num_samples, pcase->bits_per_sample,
            pcase->block_size, pcase->ch_process_method, pcase->num_encode_trials,
            pcase->rms_epsilon) != 1) {
        is_ok = 0;
        break;
      }
    }
    Test_AssertEqual(is_ok, 1);

    /* ナイキストレート振動波 */
    for (ch = 0; ch < MAX_NUM_CHANNELS; ch++) {
      for (smpl = 0; smpl < MAX_NUM_SAMPLES; smpl++) {
        input[ch][smpl] = (smpl % 2) ? INT16_MIN : INT16_MAX;
      }
    }
    
    /* 定義済みテストケースに対してテスト */
    is_ok = 1;
    for (i = 0; i < sizeof(test_case_for_nyquist) / sizeof(test_case_for_nyquist[0]); i++) {
      const struct EncodeDecodeTestForPcmDataTestCase *pcase = &test_case_for_nyquist[i];
      if (AADEncodeDecodeTest_EncodeDecodeCheckForPcmData(input, 
            pcase->num_channels, pcase->num_samples, pcase->bits_per_sample,
            pcase->block_size, pcase->ch_process_method, pcase->num_encode_trials,
            pcase->rms_epsilon) != 1) {
        is_ok = 0;
        break;
      }
    }
    Test_AssertEqual(is_ok, 1);

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
    /* FIXME: エンコードパラメータを直接含めよ */
    struct EncodeDecodeTestForWavFileTestCase {
      const char              *filename;
      uint16_t                bits_per_sample;
      uint16_t                block_size;
      AADChannelProcessMethod ch_process_method;
      uint8_t                 num_encode_trials;
      double                  rms_epsilon;
    };

    /* テストケースリスト */
    const struct EncodeDecodeTestForWavFileTestCase test_case[] = {
      { "unit_impulse_mono.wav", 4,  128, AAD_CH_PROCESS_METHOD_NONE, 0, 5.0e-2 },
      { "unit_impulse_mono.wav", 4,  256, AAD_CH_PROCESS_METHOD_NONE, 0, 5.0e-2 },
      { "unit_impulse_mono.wav", 4, 1024, AAD_CH_PROCESS_METHOD_NONE, 0, 5.0e-2 },
      { "unit_impulse_mono.wav", 4, 4096, AAD_CH_PROCESS_METHOD_NONE, 0, 5.0e-2 },
      { "unit_impulse.wav",      4,  128, AAD_CH_PROCESS_METHOD_NONE, 0, 5.0e-2 },
      { "unit_impulse.wav",      4,  128, AAD_CH_PROCESS_METHOD_MS,   0, 5.0e-2 },
      { "unit_impulse.wav",      4,  256, AAD_CH_PROCESS_METHOD_NONE, 0, 5.0e-2 },
      { "unit_impulse.wav",      4,  256, AAD_CH_PROCESS_METHOD_MS,   0, 5.0e-2 },
      { "unit_impulse.wav",      4, 1024, AAD_CH_PROCESS_METHOD_NONE, 0, 5.0e-2 },
      { "unit_impulse.wav",      4, 1024, AAD_CH_PROCESS_METHOD_MS,   0, 5.0e-2 },
      { "unit_impulse.wav",      4, 4096, AAD_CH_PROCESS_METHOD_NONE, 0, 5.0e-2 },
      { "unit_impulse.wav",      4, 4096, AAD_CH_PROCESS_METHOD_MS,   0, 5.0e-2 },
      { "sin300Hz_mono.wav",     4,  128, AAD_CH_PROCESS_METHOD_NONE, 0, 5.0e-2 },
      { "sin300Hz_mono.wav",     4,  256, AAD_CH_PROCESS_METHOD_NONE, 0, 5.0e-2 },
      { "sin300Hz_mono.wav",     4, 1024, AAD_CH_PROCESS_METHOD_NONE, 0, 5.0e-2 },
      { "sin300Hz_mono.wav",     4, 4096, AAD_CH_PROCESS_METHOD_NONE, 0, 5.0e-2 },
      { "sin300Hz.wav",          4,  128, AAD_CH_PROCESS_METHOD_NONE, 0, 5.0e-2 },
      { "sin300Hz.wav",          4,  128, AAD_CH_PROCESS_METHOD_MS,   0, 5.0e-2 },
      { "sin300Hz.wav",          4,  256, AAD_CH_PROCESS_METHOD_NONE, 0, 5.0e-2 },
      { "sin300Hz.wav",          4,  256, AAD_CH_PROCESS_METHOD_MS,   0, 5.0e-2 },
      { "sin300Hz.wav",          4, 1024, AAD_CH_PROCESS_METHOD_NONE, 0, 5.0e-2 },
      { "sin300Hz.wav",          4, 1024, AAD_CH_PROCESS_METHOD_MS,   0, 5.0e-2 },
      { "sin300Hz.wav",          4, 4096, AAD_CH_PROCESS_METHOD_NONE, 0, 5.0e-2 },
      { "sin300Hz.wav",          4, 4096, AAD_CH_PROCESS_METHOD_MS,   0, 5.0e-2 },
      { "bunny1.wav",            4,  128, AAD_CH_PROCESS_METHOD_NONE, 0, 5.0e-2 },
      { "bunny1.wav",            4,  256, AAD_CH_PROCESS_METHOD_NONE, 0, 5.0e-2 },
      { "bunny1.wav",            4, 1024, AAD_CH_PROCESS_METHOD_NONE, 0, 5.0e-2 },
      { "bunny1.wav",            4, 4096, AAD_CH_PROCESS_METHOD_NONE, 0, 5.0e-2 },
      { "pi_15-25sec.wav",       4,  128, AAD_CH_PROCESS_METHOD_NONE, 0, 5.0e-2 },
      { "pi_15-25sec.wav",       4,  128, AAD_CH_PROCESS_METHOD_MS,   0, 5.0e-2 },
      { "pi_15-25sec.wav",       4,  256, AAD_CH_PROCESS_METHOD_NONE, 0, 5.0e-2 },
      { "pi_15-25sec.wav",       4,  256, AAD_CH_PROCESS_METHOD_MS,   0, 5.0e-2 },
      { "pi_15-25sec.wav",       4, 1024, AAD_CH_PROCESS_METHOD_NONE, 0, 5.0e-2 },
      { "pi_15-25sec.wav",       4, 1024, AAD_CH_PROCESS_METHOD_MS,   0, 5.0e-2 },
      { "pi_15-25sec.wav",       4, 4096, AAD_CH_PROCESS_METHOD_NONE, 0, 5.0e-2 },
      { "pi_15-25sec.wav",       4, 4096, AAD_CH_PROCESS_METHOD_MS,   0, 5.0e-2 },
      { "unit_impulse_mono.wav", 3,  128, AAD_CH_PROCESS_METHOD_NONE, 0, 6.0e-2 },
      { "unit_impulse_mono.wav", 3,  256, AAD_CH_PROCESS_METHOD_NONE, 0, 6.0e-2 },
      { "unit_impulse_mono.wav", 3, 1024, AAD_CH_PROCESS_METHOD_NONE, 0, 6.0e-2 },
      { "unit_impulse_mono.wav", 3, 4096, AAD_CH_PROCESS_METHOD_NONE, 0, 6.0e-2 },
      { "unit_impulse.wav",      3,  128, AAD_CH_PROCESS_METHOD_NONE, 0, 6.0e-2 },
      { "unit_impulse.wav",      3,  128, AAD_CH_PROCESS_METHOD_MS,   0, 6.0e-2 },
      { "unit_impulse.wav",      3,  256, AAD_CH_PROCESS_METHOD_NONE, 0, 6.0e-2 },
      { "unit_impulse.wav",      3,  256, AAD_CH_PROCESS_METHOD_MS,   0, 6.0e-2 },
      { "unit_impulse.wav",      3, 1024, AAD_CH_PROCESS_METHOD_NONE, 0, 6.0e-2 },
      { "unit_impulse.wav",      3, 1024, AAD_CH_PROCESS_METHOD_MS,   0, 6.0e-2 },
      { "unit_impulse.wav",      3, 4096, AAD_CH_PROCESS_METHOD_NONE, 0, 6.0e-2 },
      { "unit_impulse.wav",      3, 4096, AAD_CH_PROCESS_METHOD_MS,   0, 6.0e-2 },
      { "sin300Hz_mono.wav",     3,  128, AAD_CH_PROCESS_METHOD_NONE, 0, 6.0e-2 },
      { "sin300Hz_mono.wav",     3,  256, AAD_CH_PROCESS_METHOD_NONE, 0, 6.0e-2 },
      { "sin300Hz_mono.wav",     3, 1024, AAD_CH_PROCESS_METHOD_NONE, 0, 6.0e-2 },
      { "sin300Hz_mono.wav",     3, 4096, AAD_CH_PROCESS_METHOD_NONE, 0, 6.0e-2 },
      { "sin300Hz.wav",          3,  128, AAD_CH_PROCESS_METHOD_NONE, 0, 6.0e-2 },
      { "sin300Hz.wav",          3,  128, AAD_CH_PROCESS_METHOD_MS,   0, 6.0e-2 },
      { "sin300Hz.wav",          3,  256, AAD_CH_PROCESS_METHOD_NONE, 0, 6.0e-2 },
      { "sin300Hz.wav",          3,  256, AAD_CH_PROCESS_METHOD_MS,   0, 6.0e-2 },
      { "sin300Hz.wav",          3, 1024, AAD_CH_PROCESS_METHOD_NONE, 0, 6.0e-2 },
      { "sin300Hz.wav",          3, 1024, AAD_CH_PROCESS_METHOD_MS,   0, 6.0e-2 },
      { "sin300Hz.wav",          3, 4096, AAD_CH_PROCESS_METHOD_NONE, 0, 6.0e-2 },
      { "sin300Hz.wav",          3, 4096, AAD_CH_PROCESS_METHOD_MS,   0, 6.0e-2 },
      { "bunny1.wav",            3,  128, AAD_CH_PROCESS_METHOD_NONE, 0, 6.0e-2 },
      { "bunny1.wav",            3,  256, AAD_CH_PROCESS_METHOD_NONE, 0, 6.0e-2 },
      { "bunny1.wav",            3, 1024, AAD_CH_PROCESS_METHOD_NONE, 0, 6.0e-2 },
      { "bunny1.wav",            3, 4096, AAD_CH_PROCESS_METHOD_NONE, 0, 6.0e-2 },
      { "pi_15-25sec.wav",       3,  128, AAD_CH_PROCESS_METHOD_NONE, 0, 6.0e-2 },
      { "pi_15-25sec.wav",       3,  128, AAD_CH_PROCESS_METHOD_MS,   0, 6.0e-2 },
      { "pi_15-25sec.wav",       3,  256, AAD_CH_PROCESS_METHOD_NONE, 0, 6.0e-2 },
      { "pi_15-25sec.wav",       3,  256, AAD_CH_PROCESS_METHOD_MS,   0, 6.0e-2 },
      { "pi_15-25sec.wav",       3, 1024, AAD_CH_PROCESS_METHOD_NONE, 0, 6.0e-2 },
      { "pi_15-25sec.wav",       3, 1024, AAD_CH_PROCESS_METHOD_MS,   0, 6.0e-2 },
      { "pi_15-25sec.wav",       3, 4096, AAD_CH_PROCESS_METHOD_NONE, 0, 6.0e-2 },
      { "pi_15-25sec.wav",       3, 4096, AAD_CH_PROCESS_METHOD_MS,   0, 6.0e-2 },
      { "unit_impulse_mono.wav", 2,  128, AAD_CH_PROCESS_METHOD_NONE, 0, 8.0e-2 },
      { "unit_impulse_mono.wav", 2,  256, AAD_CH_PROCESS_METHOD_NONE, 0, 8.0e-2 },
      { "unit_impulse_mono.wav", 2, 1024, AAD_CH_PROCESS_METHOD_NONE, 0, 8.0e-2 },
      { "unit_impulse_mono.wav", 2, 4096, AAD_CH_PROCESS_METHOD_NONE, 0, 8.0e-2 },
      { "unit_impulse.wav",      2,  128, AAD_CH_PROCESS_METHOD_NONE, 0, 8.0e-2 },
      { "unit_impulse.wav",      2,  128, AAD_CH_PROCESS_METHOD_MS,   0, 8.0e-2 },
      { "unit_impulse.wav",      2,  256, AAD_CH_PROCESS_METHOD_NONE, 0, 8.0e-2 },
      { "unit_impulse.wav",      2,  256, AAD_CH_PROCESS_METHOD_MS,   0, 8.0e-2 },
      { "unit_impulse.wav",      2, 1024, AAD_CH_PROCESS_METHOD_NONE, 0, 8.0e-2 },
      { "unit_impulse.wav",      2, 1024, AAD_CH_PROCESS_METHOD_MS,   0, 8.0e-2 },
      { "unit_impulse.wav",      2, 4096, AAD_CH_PROCESS_METHOD_NONE, 0, 8.0e-2 },
      { "unit_impulse.wav",      2, 4096, AAD_CH_PROCESS_METHOD_MS,   0, 8.0e-2 },
      { "sin300Hz_mono.wav",     2,  128, AAD_CH_PROCESS_METHOD_NONE, 0, 8.0e-2 },
      { "sin300Hz_mono.wav",     2,  256, AAD_CH_PROCESS_METHOD_NONE, 0, 8.0e-2 },
      { "sin300Hz_mono.wav",     2, 1024, AAD_CH_PROCESS_METHOD_NONE, 0, 8.0e-2 },
      { "sin300Hz_mono.wav",     2, 4096, AAD_CH_PROCESS_METHOD_NONE, 0, 8.0e-2 },
      { "sin300Hz.wav",          2,  128, AAD_CH_PROCESS_METHOD_NONE, 0, 8.0e-2 },
      { "sin300Hz.wav",          2,  128, AAD_CH_PROCESS_METHOD_MS,   0, 8.0e-2 },
      { "sin300Hz.wav",          2,  256, AAD_CH_PROCESS_METHOD_NONE, 0, 8.0e-2 },
      { "sin300Hz.wav",          2,  256, AAD_CH_PROCESS_METHOD_MS,   0, 8.0e-2 },
      { "sin300Hz.wav",          2, 1024, AAD_CH_PROCESS_METHOD_NONE, 0, 8.0e-2 },
      { "sin300Hz.wav",          2, 1024, AAD_CH_PROCESS_METHOD_MS,   0, 8.0e-2 },
      { "sin300Hz.wav",          2, 4096, AAD_CH_PROCESS_METHOD_NONE, 0, 8.0e-2 },
      { "bunny1.wav",            2,  128, AAD_CH_PROCESS_METHOD_NONE, 0, 8.0e-2 },
      { "bunny1.wav",            2,  256, AAD_CH_PROCESS_METHOD_NONE, 0, 8.0e-2 },
      { "bunny1.wav",            2, 1024, AAD_CH_PROCESS_METHOD_NONE, 0, 8.0e-2 },
      { "bunny1.wav",            2, 4096, AAD_CH_PROCESS_METHOD_NONE, 0, 8.0e-2 },
      { "pi_15-25sec.wav",       2,  128, AAD_CH_PROCESS_METHOD_NONE, 0, 8.0e-2 },
      { "pi_15-25sec.wav",       2,  128, AAD_CH_PROCESS_METHOD_MS,   0, 8.0e-2 },
      { "pi_15-25sec.wav",       2,  256, AAD_CH_PROCESS_METHOD_NONE, 0, 8.0e-2 },
      { "pi_15-25sec.wav",       2,  256, AAD_CH_PROCESS_METHOD_MS,   0, 8.0e-2 },
      { "pi_15-25sec.wav",       2, 1024, AAD_CH_PROCESS_METHOD_NONE, 0, 8.0e-2 },
      { "pi_15-25sec.wav",       2, 1024, AAD_CH_PROCESS_METHOD_MS,   0, 8.0e-2 },
      { "pi_15-25sec.wav",       2, 4096, AAD_CH_PROCESS_METHOD_NONE, 0, 8.0e-2 },
      { "pi_15-25sec.wav",       2, 4096, AAD_CH_PROCESS_METHOD_MS,   0, 8.0e-2 },
    };
    const uint32_t num_test_cases = sizeof(test_case) / sizeof(test_case[0]);

    /* 各テストケースを実行 */
    is_ok = 1;
    for (i = 0; i < num_test_cases; i++) {
      const struct EncodeDecodeTestForWavFileTestCase *pcase = &test_case[i];
      /* 1つでも失敗したら終わる */
      /* ログがうるさくなるのを防ぐため */
      if (AADEncodeDecodeTest_EncodeDecodeCheckForWavFile(pcase->filename,
            pcase->bits_per_sample, pcase->block_size, pcase->ch_process_method, pcase->num_encode_trials,
            pcase->rms_epsilon) != 1) {
        fprintf(stderr,
            "Encode/Decode Test Failed for %s bps:%d block size:%d Ch process method:%d RMS epsilon:%e \n",
            pcase->filename, pcase->bits_per_sample, pcase->block_size, pcase->ch_process_method, pcase->rms_epsilon);
        is_ok = 0;
        break;
      }
    }
    Test_AssertEqual(is_ok, 1);
  }
}

void AADEncodeDecodeTest_Setup(void)
{
  struct TestSuite *suite
    = Test_AddTestSuite("AAD Encode Decode Test Suite",
        NULL, AADEncodeDecodeTest_Initialize, AADEncodeDecodeTest_Finalize);

  Test_AddTest(suite, AADEncodeDecodeTest_EncodeDecodeHeaderTest);
  Test_AddTest(suite, AADEncodeDecodeTest_EncodeDecodeTest);
}
