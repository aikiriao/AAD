#include "aad_tables.h"
#include <stddef.h>

/* インデックス変動テーブルの要素定義マクロ */
#define AAD_TABLES_DEFINE_INDEX_TABLE_ENTRY(flt)  (int16_t)((flt) * (1 << AAD_TABLES_FLOAT_DIGITS))

/* インデックス変動テーブル: 4bit */
static const int16_t AAD_index_table_4bit[16] = {
  AAD_TABLES_DEFINE_INDEX_TABLE_ENTRY(-1.17),
  AAD_TABLES_DEFINE_INDEX_TABLE_ENTRY(-1.07),
  AAD_TABLES_DEFINE_INDEX_TABLE_ENTRY(-0.9),
  AAD_TABLES_DEFINE_INDEX_TABLE_ENTRY( 1),
  AAD_TABLES_DEFINE_INDEX_TABLE_ENTRY( 2),
  AAD_TABLES_DEFINE_INDEX_TABLE_ENTRY( 4),
  AAD_TABLES_DEFINE_INDEX_TABLE_ENTRY( 8),
  AAD_TABLES_DEFINE_INDEX_TABLE_ENTRY(16),
  AAD_TABLES_DEFINE_INDEX_TABLE_ENTRY(-1.17),
  AAD_TABLES_DEFINE_INDEX_TABLE_ENTRY(-1.07),
  AAD_TABLES_DEFINE_INDEX_TABLE_ENTRY(-0.9),
  AAD_TABLES_DEFINE_INDEX_TABLE_ENTRY( 1),
  AAD_TABLES_DEFINE_INDEX_TABLE_ENTRY( 2),
  AAD_TABLES_DEFINE_INDEX_TABLE_ENTRY( 4),
  AAD_TABLES_DEFINE_INDEX_TABLE_ENTRY( 8),
  AAD_TABLES_DEFINE_INDEX_TABLE_ENTRY(16),
};

/* インデックス変動テーブル: 3bit */
static const int16_t AAD_index_table_3bit[8] = {
  AAD_TABLES_DEFINE_INDEX_TABLE_ENTRY(-1.06),
  AAD_TABLES_DEFINE_INDEX_TABLE_ENTRY(-0.95),
  AAD_TABLES_DEFINE_INDEX_TABLE_ENTRY( 2),
  AAD_TABLES_DEFINE_INDEX_TABLE_ENTRY( 8),
  AAD_TABLES_DEFINE_INDEX_TABLE_ENTRY(-1.06),
  AAD_TABLES_DEFINE_INDEX_TABLE_ENTRY(-0.95),
  AAD_TABLES_DEFINE_INDEX_TABLE_ENTRY( 2),
  AAD_TABLES_DEFINE_INDEX_TABLE_ENTRY( 8),
};

/* インデックス変動テーブル: 2bit */
static const int16_t AAD_index_table_2bit[4] = {
  AAD_TABLES_DEFINE_INDEX_TABLE_ENTRY(-0.9),
  AAD_TABLES_DEFINE_INDEX_TABLE_ENTRY( 2.5),
  AAD_TABLES_DEFINE_INDEX_TABLE_ENTRY(-0.9),
  AAD_TABLES_DEFINE_INDEX_TABLE_ENTRY( 2.5),
};

#if 0
/* インデックス変動テーブル: 1bit */
/* Future work... */
static const int16_t AAD_index_table_1bit[2] = {
  AAD_TABLES_DEFINE_INDEX_TABLE_ENTRY(-1),
  AAD_TABLES_DEFINE_INDEX_TABLE_ENTRY( 2),
};
#endif

/* ステップサイズ量子化テーブル */
/* x ** 1.1 + 2 ** (log2(32767 - 255 ** 1.1) / 255 * x) で生成 */
static const uint16_t AAD_stepsize_table[AAD_STEPSIZE_TABLE_SIZE] = {
  1, 2, 3, 4, 6, 7, 8, 10,
  11, 13, 14, 16, 17, 18, 20, 22,
  23, 25, 26, 28, 29, 31, 32, 34,
  36, 37, 39, 41, 42, 44, 46, 47,
  49, 51, 52, 54, 56, 58, 59, 61,
  63, 65, 67, 68, 70, 72, 74, 76,
  78, 80, 82, 84, 86, 87, 89, 92,
  94, 96, 98, 100, 102, 104, 106, 108,
  111, 113, 115, 117, 120, 122, 124, 127,
  129, 132, 134, 137, 139, 142, 145, 147,
  150, 153, 156, 158, 161, 164, 167, 171,
  174, 177, 180, 184, 187, 190, 194, 198,
  201, 205, 209, 213, 217, 221, 226, 230,
  235, 239, 244, 249, 254, 259, 264, 270,
  275, 281, 287, 293, 299, 306, 312, 319,
  326, 333, 341, 349, 357, 365, 373, 382,
  391, 401, 411, 421, 431, 442, 453, 464,
  476, 489, 502, 515, 529, 543, 558, 573,
  589, 605, 622, 640, 658, 677, 697, 717,
  739, 761, 784, 808, 832, 858, 885, 912,
  941, 971, 1002, 1034, 1068, 1103, 1139, 1177,
  1216, 1257, 1299, 1343, 1389, 1436, 1486, 1537,
  1591, 1646, 1704, 1765, 1827, 1892, 1960, 2031,
  2104, 2181, 2260, 2343, 2429, 2519, 2612, 2709,
  2810, 2915, 3025, 3139, 3257, 3381, 3509, 3643,
  3782, 3927, 4078, 4235, 4399, 4569, 4746, 4931,
  5123, 5323, 5531, 5748, 5974, 6209, 6454, 6709,
  6974, 7250, 7538, 7838, 8150, 8475, 8813, 9165,
  9532, 9914, 10312, 10726, 11158, 11607, 12075, 12562,
  13070, 13598, 14149, 14722, 15319, 15940, 16588, 17262,
  17964, 18695, 19457, 20250, 21076, 21936, 22832, 23765,
  24737, 25749, 26803, 27901, 29044, 30235, 31475, 32767,
};

/* テーブルの初期化 */
void AADTable_Initialize(struct AADTable *table, uint16_t bits_per_sample)
{
  AAD_ASSERT(table != NULL);

  switch (bits_per_sample) {
  case 4:
    table->index_table = AAD_index_table_4bit;
    table->index_table_size = AAD_NUM_TABLE_ELEMENTS(AAD_index_table_4bit);
    break;
  case 3:
    table->index_table = AAD_index_table_3bit;
    table->index_table_size = AAD_NUM_TABLE_ELEMENTS(AAD_index_table_3bit);
    break;
  case 2:
    table->index_table = AAD_index_table_2bit;
    table->index_table_size = AAD_NUM_TABLE_ELEMENTS(AAD_index_table_2bit);
    break;
  default:
    AAD_ASSERT(0);
  }

  table->stepsize_index = 0;
  table->stepsize_table = &AAD_stepsize_table[0];
}
