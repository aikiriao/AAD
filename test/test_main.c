#include "test.h"

/* 各テストスイートのセットアップ関数宣言 */
void testByteArray_Setup(void);
void testAAD_Setup(void);

/* テスト実行 */
int main(int argc, char **argv)
{
  int ret;

  TEST_UNUSED_PARAMETER(argc);
  TEST_UNUSED_PARAMETER(argv);

  Test_Initialize();

  testByteArray_Setup();
  testAAD_Setup();

  ret = Test_RunAllTestSuite();

  Test_PrintAllFailures();

  Test_Finalize();

  return ret;
}
