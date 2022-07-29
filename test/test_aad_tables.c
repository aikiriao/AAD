#include "test.h"
#include <stdlib.h>
#include <string.h>

/* テスト対象のモジュール */
#include "../aad_tables.c"

/* テストのセットアップ関数 */
void AADTablesTest_Setup(void);

static int AADTablesTest_Initialize(void *obj)
{
  TEST_UNUSED_PARAMETER(obj);
  return 0;
}

static int AADTablesTest_Finalize(void *obj)
{
  TEST_UNUSED_PARAMETER(obj);
  return 0;
}

/* ダミー */
static void AADTablesTest_Dummy(void *obj)
{
  TEST_UNUSED_PARAMETER(obj);
}

void AADTablesTest_Setup(void)
{
  struct TestSuite *suite
    = Test_AddTestSuite("AAD Tables Test Suite",
        NULL, AADTablesTest_Initialize, AADTablesTest_Finalize);

  Test_AddTest(suite, AADTablesTest_Dummy);
}
