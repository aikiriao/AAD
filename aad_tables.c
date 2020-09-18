#include <stdint.h>

/* 多重インクルード防止 */
#ifndef AAD_TABLES_C_INCLUDED
#define AAD_TABLES_C_INCLUDED

/* インデックス変動テーブル: 4bit */
static const int8_t AAD_index_table_4bit[16] = {
  -1, -1, -1, -1, 2, 4, 6, 8, 
  -1, -1, -1, -1, 2, 4, 6, 8 
};

/* インデックス変動テーブル: 3bit */
static const int8_t AAD_index_table_3bit[8] = {
  -1, -1, 2, 4,
  -1, -1, 2, 4,
};

/* インデックス変動テーブル: 2bit */
static const int8_t AAD_index_table_2bit[4] = {
  -1, 2,
  -1, 2,
};

#if 0
/* インデックス変動テーブル: 1bit */
/* Future work... */
static const int8_t AAD_index_table_1bit[2] = {
  -1, 2,
};
#endif

/* ステップサイズ量子化テーブル */
/* x + 2 ** (log2(32767 - 255) / 255 * x) で生成 */
static const uint16_t AAD_stepsize_table[256] = {
  1, 2, 3, 4, 5, 6, 7, 8,
  9, 10, 12, 13, 14, 15, 16, 17,
  18, 19, 20, 21, 22, 23, 24, 26,
  27, 28, 29, 30, 31, 32, 33, 35,
  36, 37, 38, 39, 40, 42, 43, 44,
  45, 46, 48, 49, 50, 51, 53, 54,
  55, 56, 58, 59, 60, 62, 63, 64,
  66, 67, 69, 70, 72, 73, 75, 76,
  78, 79, 81, 82, 84, 86, 87, 89,
  91, 93, 94, 96, 98, 100, 102, 104,
  106, 108, 110, 112, 115, 117, 119, 122,
  124, 127, 129, 132, 134, 137, 140, 143,
  146, 149, 152, 155, 159, 162, 166, 169,
  173, 177, 181, 185, 189, 194, 198, 203,
  208, 213, 218, 223, 229, 235, 240, 247,
  253, 259, 266, 273, 280, 288, 296, 304,
  312, 321, 330, 339, 349, 359, 369, 380,
  391, 403, 415, 427, 440, 454, 468, 482,
  497, 513, 529, 546, 564, 582, 601, 621,
  641, 663, 685, 708, 732, 757, 783, 810,
  838, 867, 897, 929, 962, 996, 1031, 1068,
  1107, 1147, 1189, 1232, 1277, 1324, 1373, 1424,
  1477, 1532, 1589, 1649, 1711, 1776, 1843, 1913,
  1986, 2062, 2141, 2223, 2309, 2398, 2491, 2588,
  2688, 2793, 2902, 3016, 3134, 3257, 3386, 3519,
  3658, 3803, 3954, 4111, 4274, 4445, 4622, 4807,
  4999, 5199, 5408, 5625, 5851, 6086, 6332, 6587,
  6853, 7130, 7418, 7719, 8032, 8358, 8697, 9050,
  9418, 9802, 10201, 10617, 11050, 11501, 11970, 12460,
  12969, 13500, 14053, 14628, 15228, 15852, 16503, 17180,
  17885, 18620, 19385, 20182, 21013, 21877, 22778, 23716,
  24693, 25710, 26770, 27874, 29023, 30221, 31468, 32767,
};

#endif /* AAD_TABLES_C_INCLUDED */