#include "test.h"

/* 各テストスイートのセットアップ関数宣言 */
void ByteArrayTest_Setup(void);
void AADTablesTest_Setup(void);
void AADEncoderTest_Setup(void);
void AADDecoderTest_Setup(void);
void AADEncodeDecodeTest_Setup(void);

/* テスト実行 */
int main(int argc, char **argv)
{
  int ret;

  TEST_UNUSED_PARAMETER(argc);
  TEST_UNUSED_PARAMETER(argv);

  Test_Initialize();

  ByteArrayTest_Setup();
  AADTablesTest_Setup();
  AADEncoderTest_Setup();
  AADDecoderTest_Setup();
  AADEncodeDecodeTest_Setup();

  ret = Test_RunAllTestSuite();

  Test_PrintAllFailures();

  Test_Finalize();

  return ret;
}
