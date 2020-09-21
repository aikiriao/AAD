/* This program is free software. It comes without any warranty, to
 * the extent permitted by applicable law. You can redistribute it
 * and/or modify it under the terms of the Do What The Fuck You Want
 * To Public License, Version 2, as published by Sam Hocevar. See
 * http://www.wtfpl.net/ for more details. */

#include "aad.h"
#include "aad_encoder.h"
#include "aad_decoder.h"
#include "wav.h"
#include "command_line_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <math.h>

/* バージョン文字列 */
#define AADCUI_VERSION_STRING  "1.4.0"

/* コマンドライン仕様 */
static struct CommandLineParserSpecification command_line_spec[] = {
  { 'e', "encode", COMMAND_LINE_PARSER_FALSE, 
    "Encode mode (wav file -> .aad file)", 
    NULL, COMMAND_LINE_PARSER_FALSE },
  { 'd', "decode", COMMAND_LINE_PARSER_FALSE, 
    "Decode mode (.aad file -> wav file)", 
    NULL, COMMAND_LINE_PARSER_FALSE },
  { 'r', "reconstruct", COMMAND_LINE_PARSER_FALSE, 
    "Reconstruction mode (wav file -> (encode -> decode) -> decoded wav file)",
    NULL, COMMAND_LINE_PARSER_FALSE },
  { 'g', "gap", COMMAND_LINE_PARSER_FALSE, 
    "Gap(residual output) mode (wav file -> (encode -> decode) -> residual wav file)",
    NULL, COMMAND_LINE_PARSER_FALSE },
  { 'c', "calculate", COMMAND_LINE_PARSER_FALSE, 
    "Calculate statistics(e.g. RMS error) between original and reconstructed wav", 
    NULL, COMMAND_LINE_PARSER_FALSE },
  { 'i', "information", COMMAND_LINE_PARSER_FALSE, 
    "Show information of encoded .aad file", 
    NULL, COMMAND_LINE_PARSER_FALSE },
  { 'b', "bits-per-sample", COMMAND_LINE_PARSER_TRUE, 
    "Specify bits per sample(in 2,3,4) (default: 4)", 
    "4", COMMAND_LINE_PARSER_FALSE },
  { 's', "max-block-size", COMMAND_LINE_PARSER_TRUE, 
    "Specify max block size (default: 1024)", 
    "1024", COMMAND_LINE_PARSER_FALSE },
  { 't', "num-encode-trials", COMMAND_LINE_PARSER_TRUE, 
    "Specify number of encode Trials (default: 2)", 
    "2", COMMAND_LINE_PARSER_FALSE },
  { 'm', "ms-conversion", COMMAND_LINE_PARSER_FALSE, 
    "Switch to use LR to MS conversion (default: no)", 
    NULL, COMMAND_LINE_PARSER_FALSE },
  { 'h', "help", COMMAND_LINE_PARSER_FALSE, 
    "Show help message", 
    NULL, COMMAND_LINE_PARSER_FALSE },
  { 'v', "version", COMMAND_LINE_PARSER_FALSE, 
    "Show version information", 
    NULL, COMMAND_LINE_PARSER_FALSE },
  { 0, }
};

/* デコード処理 */
static int execute_decode(const char *adpcm_filename, const char *decoded_filename)
{
  FILE                      *fp;
  struct stat               fstat;
  uint8_t                   *buffer;
  uint32_t                  buffer_size;
  struct AADDecoder         *decoder;
  struct AADHeaderInfo      header;
  struct WAVFile            *wav;
  struct WAVFileFormat      wavformat;
  int32_t                   *output[AAD_MAX_NUM_CHANNELS];
  uint32_t                  ch, smpl;
  AADApiResult              ret;

  /* ファイルオープン */
  fp = fopen(adpcm_filename, "rb");
  if (fp == NULL) {
    fprintf(stderr, "Failed to open %s. \n", adpcm_filename);
    return 1;
  }

  /* 入力ファイルのサイズ取得 / バッファ領域割り当て */
  stat(adpcm_filename, &fstat);
  buffer_size = (uint32_t)fstat.st_size;
  buffer = (uint8_t *)malloc(buffer_size);
  /* バッファ領域にデータをロード */
  fread(buffer, sizeof(uint8_t), buffer_size, fp);
  fclose(fp);

  /* デコーダ作成 */
  decoder = AADDecoder_Create(NULL, 0);

  /* ヘッダ読み取り */
  if ((ret = AADDecoder_DecodeHeader(buffer, buffer_size, &header))
      != AAD_APIRESULT_OK) {
    fprintf(stderr, "Failed to read header. API result: %d \n", ret);
    return 1;
  }

  /* 出力バッファ領域確保 */
  for (ch = 0; ch < header.num_channels; ch++) {
    output[ch] = malloc(sizeof(int32_t) * header.num_samples);
  }

  /* 全データをデコード */
  if ((ret = AADDecoder_DecodeWhole(decoder, 
        buffer, buffer_size, output, 
        header.num_channels, header.num_samples)) != AAD_APIRESULT_OK) {
    fprintf(stderr, "Failed to decode. API result: %d \n", ret);
    return 1;
  }

  /* 出力ファイルを作成 */
  wavformat.data_format = WAV_DATA_FORMAT_PCM;
  wavformat.num_channels = header.num_channels;
  wavformat.sampling_rate = header.sampling_rate;
  wavformat.bits_per_sample = 16;
  wavformat.num_samples = header.num_samples;
  wav = WAV_Create(&wavformat);

  /* PCM書き出し */
  for (ch = 0; ch < header.num_channels; ch++) {
    for (smpl = 0; smpl < header.num_samples; smpl++) {
      WAVFile_PCM(wav, smpl, ch) = (output[ch][smpl] << 16);
    }
  }

  WAV_WriteToFile(decoded_filename, wav);

  AADDecoder_Destroy(decoder);
  for (ch = 0; ch < header.num_channels; ch++) {
    free(output[ch]);
  }
  WAV_Destroy(wav);
  free(buffer);

  return 0;
}

/* エンコード処理 */
static int execute_encode(
    const char *wav_file, const char *encoded_filename, const struct AADEncodeParameter *encode_paramemter)
{
  FILE                      *fp;
  struct WAVFile            *wavfile;
  struct stat               fstat;
  int32_t                   *input[AAD_MAX_NUM_CHANNELS];
  uint32_t                  ch, smpl, buffer_size, output_size;
  uint32_t                  num_channels, num_samples;
  uint8_t                   *buffer;
  struct AADEncodeParameter enc_param;
  struct AADEncoder         *encoder;
  AADApiResult              api_result;

  /* 入力wav取得 */
  wavfile = WAV_CreateFromFile(wav_file);
  if (wavfile == NULL) {
    fprintf(stderr, "Failed to open %s. \n", wav_file);
    return 1;
  }

  num_channels = wavfile->format.num_channels;
  num_samples = wavfile->format.num_samples;

  /* 出力データの領域割当て */
  for (ch = 0; ch < num_channels; ch++) {
    input[ch] = malloc(sizeof(int32_t) * num_samples);
  }
  /* 入力wavと同じサイズの出力領域を確保（増えることはないと期待） */
  stat(wav_file, &fstat);
  buffer_size = (uint32_t)fstat.st_size;
  buffer = malloc(buffer_size);

  /* 16bit幅でデータ取得 */
  for (ch = 0; ch < num_channels; ch++) {
    for (smpl = 0; smpl < num_samples; smpl++) {
      input[ch][smpl] = (int16_t)(WAVFile_PCM(wavfile, smpl, ch) >> 16);
    }
  }

  /* ハンドル作成 */
  encoder = AADEncoder_Create(encode_paramemter->max_block_size, NULL, 0);

  /* エンコードパラメータをセット */
  enc_param.num_channels      = (uint16_t)num_channels;
  enc_param.sampling_rate     = wavfile->format.sampling_rate;
  enc_param.bits_per_sample   = encode_paramemter->bits_per_sample;
  enc_param.max_block_size    = encode_paramemter->max_block_size;
  enc_param.ch_process_method = encode_paramemter->ch_process_method;
  enc_param.num_encode_trials = encode_paramemter->num_encode_trials;
  if ((api_result = AADEncoder_SetEncodeParameter(encoder, &enc_param))
      != AAD_APIRESULT_OK) {
    fprintf(stderr, "Failed to set encode parameter. Please check encode parameter. \n");
    return 1;
  }

  /* エンコード */
  if ((api_result = AADEncoder_EncodeWhole(
        encoder, (const int32_t *const *)input, num_samples,
        buffer, buffer_size, &output_size)) != AAD_APIRESULT_OK) {
    fprintf(stderr, "Failed to encode. API result:%d \n", api_result);
    return 1;
  }

  /* ファイル書き出し */
  fp = fopen(encoded_filename, "wb");
  if (fp == NULL) {
    fprintf(stderr, "Failed to open output file %s \n", encoded_filename);
    return 1;
  }
  if (fwrite(buffer, sizeof(uint8_t), output_size, fp) < output_size) {
    fprintf(stderr, "Warning: failed to write encoded data \n");
    return 1;
  }
  fclose(fp);

  /* 領域開放 */
  AADEncoder_Destroy(encoder);
  free(buffer);
  for (ch = 0; ch < num_channels; ch++) {
    free(input[ch]);
  }
  WAV_Destroy(wavfile);

  return 0;
}

/* ヘッダ情報の表示 */
static int execute_information(const char *adpcm_filename)
{
  FILE                  *fp;
  uint8_t               buffer[AAD_HEADER_SIZE];
  struct AADHeaderInfo  header;
  AADApiResult          ret;

  /* ファイルオープン */
  fp = fopen(adpcm_filename, "rb");
  if (fp == NULL) {
    fprintf(stderr, "Failed to open %s. \n", adpcm_filename);
    return 1;
  }

  /* ヘッダだけ読み込み */
  if (fread(buffer, sizeof(uint8_t), AAD_HEADER_SIZE, fp) < AAD_HEADER_SIZE) {
    fprintf(stderr, "Failed to read from %s. \n", adpcm_filename);
    fclose(fp);
    return 1;
  }
  fclose(fp);

  /* ヘッダデコード */
  if ((ret = AADDecoder_DecodeHeader(buffer, AAD_HEADER_SIZE, &header))
      != AAD_APIRESULT_OK) {
    fprintf(stderr, "Failed to read header. API result: %d \n", ret);
    return 1;
  }

  /* ヘッダ情報表示 */
  printf("Number of Channels: %d \n",             header.num_channels);
  printf("Number of Samples per Channel: %d \n",  header.num_samples);
  printf("Sampling Rate: %d \n",                  header.sampling_rate);
  printf("Bits per Sample: %d \n",                header.bits_per_sample);
  printf("Block size: %d \n",                     header.block_size);
  printf("Number of Samples per Block: %d \n",    header.num_samples_per_block);
  printf("Method of Channel Processing : %d \n",  header.ch_process_method);

  return 0;
}

/* 再構成コア処理 */
static int execute_reconstruction_core(
    const struct WAVFile *in_wav, int32_t **decoded, const struct AADEncodeParameter *encode_paramemter)
{
  int32_t                   *pcmdata[AAD_MAX_NUM_CHANNELS];
  uint32_t                  ch, smpl, buffer_size, output_size;
  uint32_t                  num_channels, num_samples;
  uint8_t                   *buffer;
  struct AADEncodeParameter enc_param;
  struct AADEncoder         *encoder;
  struct AADDecoder         *decoder;
  AADApiResult              api_result;

  num_channels  = in_wav->format.num_channels;
  num_samples   = in_wav->format.num_samples;

  /* 出力データの領域割当て */
  for (ch = 0; ch < num_channels; ch++) {
    pcmdata[ch] = malloc(sizeof(int32_t) * num_samples);
  }
  /* 入力wavPCMと同等の出力領域を確保（増えることはないと期待） */
  buffer_size = (uint32_t)(sizeof(int32_t) * num_channels * num_samples);
  buffer = malloc(buffer_size);

  /* 16bit幅でデータ取得 */
  for (ch = 0; ch < num_channels; ch++) {
    for (smpl = 0; smpl < num_samples; smpl++) {
      pcmdata[ch][smpl] = (int32_t)(WAVFile_PCM(in_wav, smpl, ch) >> 16);
    }
  }

  /* ハンドル作成 */
  encoder = AADEncoder_Create(encode_paramemter->max_block_size, NULL, 0);
  decoder = AADDecoder_Create(NULL, 0);

  /* エンコードパラメータをセット */
  enc_param.num_channels      = (uint16_t)num_channels;
  enc_param.sampling_rate     = in_wav->format.sampling_rate;
  enc_param.bits_per_sample   = encode_paramemter->bits_per_sample;
  enc_param.max_block_size    = encode_paramemter->max_block_size;
  enc_param.ch_process_method = encode_paramemter->ch_process_method;
  enc_param.num_encode_trials = encode_paramemter->num_encode_trials;
  if ((api_result = AADEncoder_SetEncodeParameter(encoder, &enc_param))
      != AAD_APIRESULT_OK) {
    fprintf(stderr, "Failed to set encode parameter. Please check encode parameter. \n");
    return 1;
  }

  /* エンコード */
  if ((api_result = AADEncoder_EncodeWhole(
        encoder, (const int32_t *const *)pcmdata, num_samples,
        buffer, buffer_size, &output_size)) != AAD_APIRESULT_OK) {
    fprintf(stderr, "Failed to encode. API result:%d \n", api_result);
    return 1;
  }

  /* そのままデコード */
  if ((api_result = AADDecoder_DecodeWhole(decoder, 
        buffer, output_size, decoded, num_channels, num_samples)) != AAD_APIRESULT_OK) {
    fprintf(stderr, "Failed to decode. API result: %d \n", api_result);
    return 1;
  }

  /* 領域開放 */
  AADEncoder_Destroy(encoder);
  AADDecoder_Destroy(decoder);
  free(buffer);
  for (ch = 0; ch < num_channels; ch++) {
    free(pcmdata[ch]);
  }

  return 0;
}

/* 再構成処理 */
static int execute_reconstruction(
    const char *wav_file, const char *reconstruct_file, const struct AADEncodeParameter *encode_paramemter)
{
  int             ret;
  struct WAVFile  *wavfile;
  int32_t         *pcmdata[AAD_MAX_NUM_CHANNELS];
  uint32_t        ch, smpl;
  uint32_t        num_channels, num_samples;

  /* 入力wav取得 */
  wavfile = WAV_CreateFromFile(wav_file);
  if (wavfile == NULL) {
    fprintf(stderr, "Failed to open %s. \n", wav_file);
    return 1;
  }

  num_channels = wavfile->format.num_channels;
  num_samples = wavfile->format.num_samples;

  /* 出力データの領域割当て */
  for (ch = 0; ch < num_channels; ch++) {
    pcmdata[ch] = malloc(sizeof(int32_t) * num_samples);
  }

  /* 再構成処理実行 */
  if ((ret = execute_reconstruction_core(wavfile, pcmdata, encode_paramemter)) != 0) {
    return ret;
  }

  /* デコード結果を出力にセット */
  for (ch = 0; ch < num_channels; ch++) {
    for (smpl = 0; smpl < num_samples; smpl++) {
      WAVFile_PCM(wavfile, smpl, ch) = pcmdata[ch][smpl] << 16;
    }
  }

  /* ファイルに書き出し */
  WAV_WriteToFile(reconstruct_file, wavfile);

  return 0;
}

/* 残差出力処理 */
static int execute_gap(
    const char *wav_file, const char *gap_file, const struct AADEncodeParameter *encode_paramemter)
{
  int             ret;
  struct WAVFile  *wavfile;
  int32_t         *pcmdata[AAD_MAX_NUM_CHANNELS];
  uint32_t        ch, smpl;
  uint32_t        num_channels, num_samples;

  /* 入力wav取得 */
  wavfile = WAV_CreateFromFile(wav_file);
  if (wavfile == NULL) {
    fprintf(stderr, "Failed to open %s. \n", wav_file);
    return 1;
  }

  num_channels = wavfile->format.num_channels;
  num_samples = wavfile->format.num_samples;

  /* 出力データの領域割当て */
  for (ch = 0; ch < num_channels; ch++) {
    pcmdata[ch] = malloc(sizeof(int32_t) * num_samples);
  }

  /* 再構成処理実行 */
  if ((ret = execute_reconstruction_core(wavfile, pcmdata, encode_paramemter)) != 0) {
    return ret;
  }

  /* 原音から差し引いて残差計算 */
  for (ch = 0; ch < num_channels; ch++) {
    for (smpl = 0; smpl < num_samples; smpl++) {
      WAVFile_PCM(wavfile, smpl, ch) -= (pcmdata[ch][smpl] << 16);
    }
  }

  /* ファイルに書き出し */
  WAV_WriteToFile(gap_file, wavfile);

  return 0;
}

/* 統計情報出力 */
static int execute_calculation(const char *wav_file, const struct AADEncodeParameter *encode_paramemter)
{
  int             ret;
  struct WAVFile  *wavfile;
  int32_t         *pcmdata[AAD_MAX_NUM_CHANNELS];
  uint32_t        ch, smpl;
  uint32_t        num_channels, num_samples;
  double          rms_error, max_error, abs_error;

  /* 入力wav取得 */
  wavfile = WAV_CreateFromFile(wav_file);
  if (wavfile == NULL) {
    fprintf(stderr, "Failed to open %s. \n", wav_file);
    return 1;
  }

  num_channels = wavfile->format.num_channels;
  num_samples = wavfile->format.num_samples;

  /* 出力データの領域割当て */
  for (ch = 0; ch < num_channels; ch++) {
    pcmdata[ch] = malloc(sizeof(int32_t) * num_samples);
  }

  /* 再構成処理実行 */
  if ((ret = execute_reconstruction_core(wavfile, pcmdata, encode_paramemter)) != 0) {
    return ret;
  }

  /* 原音から差し引いて残差計算 */
  for (ch = 0; ch < num_channels; ch++) {
    for (smpl = 0; smpl < num_samples; smpl++) {
      WAVFile_PCM(wavfile, smpl, ch) -= (pcmdata[ch][smpl] << 16);
    }
  }

  /* 統計情報計算 */
  rms_error = 0.0f;
  max_error = 0.0f;
  abs_error = 0.0f;
  for (ch = 0; ch < num_channels; ch++) {
    for (smpl = 0; smpl < num_samples; smpl++) {
      double pcm1, pcm2;
      pcm1 = (double)WAVFile_PCM(wavfile, smpl, ch) / INT32_MAX;
      pcm2 = (double)pcmdata[ch][smpl] / INT32_MAX;
      rms_error += pow(pcm1 - pcm2, 2);
      abs_error += fabs(pcm1 - pcm2);
      if (max_error < fabs(pcm1 - pcm2)) {
        max_error = fabs(pcm1 - pcm2);
      }
    }
  }

  printf("RMSE:%f MSD:%f MaxAE:%f \n", 
      sqrt(rms_error / (num_channels * num_samples)), 
      abs_error / (num_channels * num_samples),
      max_error);

  return 0;
}

/* 使用法の表示 */
static void print_usage(const char* program_name)
{
  printf("Usage: %s [options] INPUT_FILE_NAME OUTPUT_FILE_NAME \n", program_name);
}

/* バージョン情報の表示 */
static void print_version_info(void)
{
  printf("AAD(Ayashi Adaptive Differential pulse code modulation) encoder/decoder Version.%s \n", AADCUI_VERSION_STRING);
}

/* メインエントリ */
int main(int argc, char **argv)
{
  uint32_t num_modes_specified;
  const char *filename_ptr[2] = { NULL, NULL };
  const char *in_filename, *out_filename;
  struct AADEncodeParameter encode_paramemter = { 0, };

  /* 引数の数が想定外 */
  if (argc == 1) {
    print_usage(argv[0]);
    printf("type `%s -h` to display usage. \n", argv[0]);
    return 1;
  }

  /* コマンドライン解析 */
  if (CommandLineParser_ParseArguments(command_line_spec,
        argc, argv, filename_ptr, sizeof(filename_ptr) / sizeof(filename_ptr[0]))
      != COMMAND_LINE_PARSER_RESULT_OK) {
    return 1;
  }

  /* ヘルプやバージョン情報の表示判定 */
  if (CommandLineParser_GetOptionAcquired(command_line_spec, "help") == COMMAND_LINE_PARSER_TRUE) {
    print_usage(argv[0]);
    printf("options: \n");
    CommandLineParser_PrintDescription(command_line_spec);
    return 0;
  } else if (CommandLineParser_GetOptionAcquired(command_line_spec, "version") == COMMAND_LINE_PARSER_TRUE) {
    print_version_info();
    return 0;
  }

  /* 指定されたモードの数 */
  num_modes_specified
    = CommandLineParser_GetOptionAcquired(command_line_spec, "decode")
    + CommandLineParser_GetOptionAcquired(command_line_spec, "encode")
    + CommandLineParser_GetOptionAcquired(command_line_spec, "information")
    + CommandLineParser_GetOptionAcquired(command_line_spec, "reconstruct")
    + CommandLineParser_GetOptionAcquired(command_line_spec, "gap")
    + CommandLineParser_GetOptionAcquired(command_line_spec, "calculate");

  /* 1つもモードが指定されていない */
  if (num_modes_specified == 0) {
      fprintf(stderr, "%s: must specify at least one mode. \n", argv[0]);
      return 1;
  }

  /* 2つ以上のモードが指定された */
  if (num_modes_specified >= 2) {
      fprintf(stderr, "%s: multiple modes cannot specify simultaneously. \n", argv[0]);
      return 1;
  }

  /* 入力ファイル名の取得 */
  if ((in_filename = filename_ptr[0]) == NULL) {
    fprintf(stderr, "%s: input file must be specified. \n", argv[0]);
    return 1;
  }

  /* エンコードパラメータの取得 */
  if ((CommandLineParser_GetOptionAcquired(command_line_spec, "decode") == COMMAND_LINE_PARSER_FALSE)
      && (CommandLineParser_GetOptionAcquired(command_line_spec, "information") == COMMAND_LINE_PARSER_FALSE)) {
    encode_paramemter.bits_per_sample
      = (uint8_t)strtol(CommandLineParser_GetArgumentString(command_line_spec, "bits-per-sample"), NULL, 10);
    encode_paramemter.max_block_size
      = (uint16_t)strtol(CommandLineParser_GetArgumentString(command_line_spec, "max-block-size"), NULL, 10);
    encode_paramemter.num_encode_trials
      = (uint8_t)strtol(CommandLineParser_GetArgumentString(command_line_spec, "num-encode-trials"), NULL, 10);
    encode_paramemter.ch_process_method = AAD_CH_PROCESS_METHOD_NONE;
    if (CommandLineParser_GetOptionAcquired(command_line_spec, "ms-conversion") == COMMAND_LINE_PARSER_TRUE) {
      encode_paramemter.ch_process_method = AAD_CH_PROCESS_METHOD_MS;
    }
  }

  /* 入力だけが必要な処理 */
  if (CommandLineParser_GetOptionAcquired(command_line_spec, "information") == COMMAND_LINE_PARSER_TRUE) {
    /* ヘッダ情報表示 */
    return execute_information(in_filename);
  } else if (CommandLineParser_GetOptionAcquired(command_line_spec, "calculate") == COMMAND_LINE_PARSER_TRUE) {
    /* 統計情報出力 */
    return execute_calculation(in_filename, &encode_paramemter);
  } 
  
  /* 出力ファイル名の取得 */
  if ((out_filename = filename_ptr[1]) == NULL) {
    fprintf(stderr, "%s: output file must be specified. \n", argv[0]);
    return 1;
  }

  /* 入出力が必要な処理 */
  if (CommandLineParser_GetOptionAcquired(command_line_spec, "decode") == COMMAND_LINE_PARSER_TRUE) {
    /* デコード */
    return execute_decode(in_filename, out_filename);
  } else if (CommandLineParser_GetOptionAcquired(command_line_spec, "encode") == COMMAND_LINE_PARSER_TRUE) {
    /* エンコード */
    return execute_encode(in_filename, out_filename, &encode_paramemter);
  } else if (CommandLineParser_GetOptionAcquired(command_line_spec, "reconstruct") == COMMAND_LINE_PARSER_TRUE) {
    /* 再構成 */
    return execute_reconstruction(in_filename, out_filename, &encode_paramemter);
  } else if (CommandLineParser_GetOptionAcquired(command_line_spec, "gap") == COMMAND_LINE_PARSER_TRUE) {
    /* 残差生成 */
    return execute_gap(in_filename, out_filename, &encode_paramemter);
  }

  return 1;
}
