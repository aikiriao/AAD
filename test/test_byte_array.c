#include <stdlib.h>
#include <string.h>
#include "test.h"

/* テスト対象のモジュール */
#include "../src/byte_array.h"

/* テストのセットアップ関数 */
void ByteArrayTest_Setup(void);

static int ByteArrayTest_Initialize(void *obj)
{
  TEST_UNUSED_PARAMETER(obj);
  return 0;
}

static int ByteArrayTest_Finalize(void *obj)
{
  TEST_UNUSED_PARAMETER(obj);
  return 0;
}

/* 読み書きテスト */
static void ByteArrayTest_ReadWriteTest(void *obj)
{
#define TEST_SIZE (256 * 256)
  TEST_UNUSED_PARAMETER(obj);

  /* 1バイト読み/書き */
  {
    uint8_t *pos;
    uint8_t array[TEST_SIZE], answer[TEST_SIZE];
    uint32_t i;

    /* 書き出し */
    pos = array;
    for (i = 0; i < TEST_SIZE; i++) {
      answer[i] = (uint8_t)i;
      ByteArray_WriteUint8(pos, (uint8_t)i);
      pos += 1;
    }

    /* リファレンスと一致するか？ */
    Test_AssertEqual(memcmp(array, answer, sizeof(uint8_t) * TEST_SIZE), 0);

    /* 読み出し */
    pos = array;
    for (i = 0; i < TEST_SIZE; i++) {
      array[i] = ByteArray_ReadUint8(pos);
      pos += 1;
    }

    /* リファレンスと一致するか？ */
    Test_AssertEqual(memcmp(array, answer, sizeof(uint8_t) * TEST_SIZE), 0);
  }
  /* 同じことをGet/Putでやる */
  {
    uint8_t *pos;
    uint8_t array[TEST_SIZE], answer[TEST_SIZE];
    uint32_t i;

    /* 書き出し */
    pos = array;
    for (i = 0; i < TEST_SIZE; i++) {
      answer[i] = (uint8_t)i;
      ByteArray_PutUint8(pos, (uint8_t)i);
    }

    /* リファレンスと一致するか？ */
    Test_AssertEqual(memcmp(array, answer, sizeof(uint8_t) * TEST_SIZE), 0);

    /* 読み出し */
    pos = array;
    for (i = 0; i < TEST_SIZE; i++) {
      ByteArray_GetUint8(pos, &array[i]);
    }

    /* リファレンスと一致するか？ */
    Test_AssertEqual(memcmp(array, answer, sizeof(uint8_t) * TEST_SIZE), 0);
  }

  /* 2バイト読み/書き */
  {
#define TEST_SIZE_UINT16 (TEST_SIZE / sizeof(uint16_t))
    uint8_t   *pos;
    uint8_t   array[TEST_SIZE];
    uint16_t  test[TEST_SIZE_UINT16], answer[TEST_SIZE_UINT16];
    uint32_t  i;

    /* 書き出し */
    pos = array;
    for (i = 0; i < TEST_SIZE_UINT16; i++) {
      answer[i] = (uint16_t)i;
      ByteArray_WriteUint16LE(pos, (uint16_t)i);
      pos += 2;
    }

    /* 読み出し */
    pos = array;
    for (i = 0; i < TEST_SIZE_UINT16; i++) {
      test[i] = ByteArray_ReadUint16LE(pos);
      pos += 2;
    }

    /* 読み出した結果がリファレンスと一致するか？ */
    Test_AssertEqual(memcmp(test, answer, sizeof(uint8_t) * TEST_SIZE), 0);

    /* ビッグエンディアンでも */

    /* 書き出し */
    pos = array;
    for (i = 0; i < TEST_SIZE_UINT16; i++) {
      answer[i] = (uint16_t)i;
      ByteArray_WriteUint16BE(pos, (uint16_t)i);
      pos += 2;
    }

    /* 読み出し */
    pos = array;
    for (i = 0; i < TEST_SIZE_UINT16; i++) {
      test[i] = ByteArray_ReadUint16BE(pos);
      pos += 2;
    }

    /* 読み出した結果がリファレンスと一致するか？ */
    Test_AssertEqual(memcmp(test, answer, sizeof(uint8_t) * TEST_SIZE), 0);

#undef TEST_SIZE_UINT16
  }
  /* 同じことをGet/Putでやる */
  {
#define TEST_SIZE_UINT16 (TEST_SIZE / sizeof(uint16_t))
    uint8_t   *pos;
    uint8_t   array[TEST_SIZE];
    uint16_t  test[TEST_SIZE_UINT16], answer[TEST_SIZE_UINT16];
    uint32_t  i;

    /* 書き出し */
    pos = array;
    for (i = 0; i < TEST_SIZE_UINT16; i++) {
      answer[i] = (uint16_t)i;
      ByteArray_PutUint16LE(pos, (uint16_t)i);
    }

    /* 読み出し */
    pos = array;
    for (i = 0; i < TEST_SIZE_UINT16; i++) {
      ByteArray_GetUint16LE(pos, &test[i]);
    }

    /* 読み出した結果がリファレンスと一致するか？ */
    Test_AssertEqual(memcmp(test, answer, sizeof(uint8_t) * TEST_SIZE), 0);

    /* ビッグエンディアンでも */

    /* 書き出し */
    pos = array;
    for (i = 0; i < TEST_SIZE_UINT16; i++) {
      answer[i] = (uint16_t)i;
      ByteArray_PutUint16BE(pos, (uint16_t)i);
    }

    /* 読み出し */
    pos = array;
    for (i = 0; i < TEST_SIZE_UINT16; i++) {
      ByteArray_GetUint16BE(pos, &test[i]);
    }

    /* 読み出した結果がリファレンスと一致するか？ */
    Test_AssertEqual(memcmp(test, answer, sizeof(uint8_t) * TEST_SIZE), 0);

#undef TEST_SIZE_UINT16
  }

  /* 3バイト読み/書き */
  {
#define TEST_SIZE_FOR24  1021
#define TEST_SIZE_UINT24 ((TEST_SIZE_FOR24 * 4) / (sizeof(uint32_t) * 3))
    uint8_t   *pos;
    uint8_t   array[TEST_SIZE_FOR24];
    uint32_t  test[TEST_SIZE_UINT24], answer[TEST_SIZE_UINT24];
    uint32_t  i;

    /* 書き出し */
    pos = array;
    for (i = 0; i < TEST_SIZE_UINT24; i++) {
      answer[i] = (uint32_t)i;
      ByteArray_WriteUint24LE(pos, (uint32_t)i);
      pos += 3;
    }

    /* 読み出し */
    pos = array;
    for (i = 0; i < TEST_SIZE_UINT24; i++) {
      test[i] = ByteArray_ReadUint24LE(pos);
      pos += 3;
    }

    /* 読み出した結果がリファレンスと一致するか？ */
    Test_AssertEqual(memcmp(test, answer, sizeof(uint8_t) * TEST_SIZE_FOR24), 0);

    /* ビッグエンディアンでも */

    /* 書き出し */
    pos = array;
    for (i = 0; i < TEST_SIZE_UINT24; i++) {
      answer[i] = (uint32_t)i;
      ByteArray_WriteUint24BE(pos, (uint32_t)i);
      pos += 3;
    }

    /* 読み出し */
    pos = array;
    for (i = 0; i < TEST_SIZE_UINT24; i++) {
      test[i] = ByteArray_ReadUint24BE(pos);
      pos += 3;
    }

    /* 読み出した結果がリファレンスと一致するか？ */
    Test_AssertEqual(memcmp(test, answer, sizeof(uint8_t) * TEST_SIZE_FOR24), 0);

#undef TEST_SIZE_UINT24
#undef TEST_SIZE_FOR24
  }
  /* 同じことをGet/Putでやる */
  {
#define TEST_SIZE_FOR24  1021
#define TEST_SIZE_UINT24 ((TEST_SIZE_FOR24 * 4) / (sizeof(uint32_t) * 3))
    uint8_t   *pos;
    uint8_t   array[TEST_SIZE];
    uint32_t  test[TEST_SIZE_UINT24], answer[TEST_SIZE_UINT24];
    uint32_t  i;

    /* 書き出し */
    pos = array;
    for (i = 0; i < TEST_SIZE_UINT24; i++) {
      answer[i] = (uint32_t)i;
      ByteArray_PutUint24LE(pos, (uint32_t)i);
    }

    /* 読み出し */
    pos = array;
    for (i = 0; i < TEST_SIZE_UINT24; i++) {
      ByteArray_GetUint24LE(pos, &test[i]);
    }

    /* 読み出した結果がリファレンスと一致するか？ */
    Test_AssertEqual(memcmp(test, answer, sizeof(uint8_t) * TEST_SIZE_FOR24), 0);

    /* ビッグエンディアンでも */

    /* 書き出し */
    pos = array;
    for (i = 0; i < TEST_SIZE_UINT24; i++) {
      answer[i] = (uint32_t)i;
      ByteArray_PutUint24BE(pos, (uint32_t)i);
    }

    /* 読み出し */
    pos = array;
    for (i = 0; i < TEST_SIZE_UINT24; i++) {
      ByteArray_GetUint24BE(pos, &test[i]);
    }

    /* 読み出した結果がリファレンスと一致するか？ */
    Test_AssertEqual(memcmp(test, answer, sizeof(uint8_t) * TEST_SIZE_FOR24), 0);

#undef TEST_SIZE_UINT24
#undef TEST_SIZE_FOR24
  }

  /* 4バイト読み/書き */
  {
#define TEST_SIZE_UINT32 (TEST_SIZE / sizeof(uint32_t))
    uint8_t   *pos;
    uint8_t   array[TEST_SIZE];
    uint32_t  test[TEST_SIZE_UINT32], answer[TEST_SIZE_UINT32];
    uint32_t  i;

    /* 書き出し */
    pos = array;
    for (i = 0; i < TEST_SIZE_UINT32; i++) {
      answer[i] = (uint32_t)i;
      ByteArray_WriteUint32LE(pos, (uint32_t)i);
      pos += 4;
    }

    /* 読み出し */
    pos = array;
    for (i = 0; i < TEST_SIZE_UINT32; i++) {
      test[i] = ByteArray_ReadUint32LE(pos);
      pos += 4;
    }

    /* 読み出した結果がリファレンスと一致するか？ */
    Test_AssertEqual(memcmp(test, answer, sizeof(uint8_t) * TEST_SIZE), 0);

    /* ビッグエンディアンでも */

    /* 書き出し */
    pos = array;
    for (i = 0; i < TEST_SIZE_UINT32; i++) {
      answer[i] = (uint32_t)i;
      ByteArray_WriteUint32BE(pos, (uint32_t)i);
      pos += 4;
    }

    /* 読み出し */
    pos = array;
    for (i = 0; i < TEST_SIZE_UINT32; i++) {
      test[i] = ByteArray_ReadUint32BE(pos);
      pos += 4;
    }

    /* 読み出した結果がリファレンスと一致するか？ */
    Test_AssertEqual(memcmp(test, answer, sizeof(uint8_t) * TEST_SIZE), 0);

#undef TEST_SIZE_UINT32
  }

  /* 同じことをGet/Putでやる */
  {
#define TEST_SIZE_UINT32 (TEST_SIZE / sizeof(uint32_t))
    uint8_t   *pos;
    uint8_t   array[TEST_SIZE];
    uint32_t  test[TEST_SIZE_UINT32], answer[TEST_SIZE_UINT32];
    uint32_t  i;

    /* 書き出し */
    pos = array;
    for (i = 0; i < TEST_SIZE_UINT32; i++) {
      answer[i] = (uint32_t)i;
      ByteArray_PutUint32LE(pos, (uint32_t)i);
    }

    /* 読み出し */
    pos = array;
    for (i = 0; i < TEST_SIZE_UINT32; i++) {
      ByteArray_GetUint32LE(pos, &test[i]);
    }

    /* 読み出した結果がリファレンスと一致するか？ */
    Test_AssertEqual(memcmp(test, answer, sizeof(uint8_t) * TEST_SIZE), 0);

    /* ビッグエンディアンでもやる */

    /* 書き出し */
    pos = array;
    for (i = 0; i < TEST_SIZE_UINT32; i++) {
      answer[i] = (uint32_t)i;
      ByteArray_PutUint32BE(pos, (uint32_t)i);
    }

    /* 読み出し */
    pos = array;
    for (i = 0; i < TEST_SIZE_UINT32; i++) {
      ByteArray_GetUint32BE(pos, &test[i]);
    }

    /* 読み出した結果がリファレンスと一致するか？ */
    Test_AssertEqual(memcmp(test, answer, sizeof(uint8_t) * TEST_SIZE), 0);

#undef TEST_SIZE_UINT16
  }

#undef TEST_SIZE
}

void ByteArrayTest_Setup(void)
{
  struct TestSuite *suite
    = Test_AddTestSuite("Byte Array Test Suite",
        NULL, ByteArrayTest_Initialize, ByteArrayTest_Finalize);

  Test_AddTest(suite, ByteArrayTest_ReadWriteTest);
}
