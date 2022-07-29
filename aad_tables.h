/* 多重インクルード防止 */
#ifndef AAD_TABLES_H_INCLUDED
#define AAD_TABLES_H_INCLUDED

#include <stdint.h>
#include "aad_internal.h"

/* ステップサイズのテーブルサイズ */
#define AAD_STEPSIZE_TABLE_SIZE 256
/* 固定小数部の桁数 */
#define AAD_TABLES_FLOAT_DIGITS 4
/* 固定小数の0.5 */
#define AAD_TABLES_FLOAT_0_5 (1 << (AAD_TABLES_FLOAT_DIGITS - 1))
/* 固定小数 -> ステップサイズテーブルインデックス */
#define AAD_TABLES_FLOAT_TO_INDEX(flt) (((flt) + AAD_TABLES_FLOAT_0_5) >> AAD_TABLES_FLOAT_DIGITS)
/* ステップサイズテーブルインデックス -> 固定小数 */
#define AAD_TABLES_INDEX_TO_FLOAT(idx) ((idx) << AAD_TABLES_FLOAT_DIGITS)
/* ステップサイズ取得 */
#define AAD_TABLES_GET_STEPSIZE(table) ((table)->stepsize_table[AAD_TABLES_FLOAT_TO_INDEX((table)->stepsize_index)])

/* テーブル */
struct AADTable {
  const int16_t *index_table;
  int16_t index_table_size;
  int16_t stepsize_index;
  const uint16_t *stepsize_table;
};

/* インデックスの更新 */
#define AADTable_UpdateIndex(table, code)\
  do {\
    int16_t inx = (table)->stepsize_index;\
    AAD_ASSERT((code) < (table)->index_table_size);\
    /* テーブルインデックスの更新 */\
    (inx) = (int16_t)((inx) + (table)->index_table[(code)]);\
    /* インデックスの範囲内でクリップ */\
    (inx) = AAD_INNER_VAL((inx), 0,\
        AAD_TABLES_INDEX_TO_FLOAT(AAD_STEPSIZE_TABLE_SIZE - 1));\
    /* インデックス範囲チェック */\
    AAD_ASSERT(AAD_TABLES_FLOAT_TO_INDEX(inx) < AAD_STEPSIZE_TABLE_SIZE);\
    (table)->stepsize_index = (inx);\
  } while (0)

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* テーブルの初期化 */
void AADTable_Initialize(struct AADTable *table, uint16_t bits_per_sample);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* AAD_TABLES_H_INCLUDED */
