#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <httpd.h>
#include <http_protocol.h>
#include <http_config.h>

/* board/history 32 位 integer 编码
 * bits  0-6    board[index]
 *       7-15   state  [各位数字的状态]
 *      16-19   数字
 *         20   fixed  [读取板时设置 fixed 位]
 *         21   choice [尝试选择时设置这个位]
 *      22-24   hint
 *      25-31   未使用
 */
#define INDEX_MASK 0x0000007f
#define GET_INDEX(val) (INDEX_MASK & (val))
#define SET_INDEX(val) (INDEX_MASK & (val))

#define STATE_MASK 0x0000ff80
#define STATE_SHIFT (7 - 1) /* digits 1..9 */
#define DIGIT_STATE(digit) (1 << (STATE_SHIFT + (digit)))

#define DIGIT_MASK 0x000f0000
#define DIGIT_SHIFT 16
#define GET_DIGIT(val) (((val)&DIGIT_MASK) >> (DIGIT_SHIFT))
#define SET_DIGIT(val) (((val) << (DIGIT_SHIFT)) & DIGIT_MASK)

#define FIXED 0x00100000
#define CHOICE 0x00200000

#define HINT_MASK 0x01C00000
#define HINT_ROW 0x00400000
#define HINT_COLUMN 0x00800000
#define HINT_BLOCK 0x01000000
#define GET_HINT(val) ((val)&HINT_MASK)

#define BOARDSIZE 81
#define SIZEOFBOARD (sizeof(int) * (BOARDSIZE))
#define HISTORYSIZE 81
#define SIZEOFHISTORY (sizeof(int) * (HISTORYSIZE))

#define ROW(idx) ((idx) / 9)
#define COLUMN(idx) ((idx) % 9)
#define BLOCK(idx) (3 * (ROW(idx) / 3) + (COLUMN(idx) / 3))
#define INDEX(row, col) (9 * (row) + (col))

#define IDX_BLOCK(row, col) (3 * ((row) / 3) + ((col) / 3))

#define STATE(idx) ((board[idx]) & STATE_MASK)
#define DIGIT(idx) (GET_DIGIT(board[idx]))
#define IS_EMPTY(idx) (0 == DIGIT(idx))
#define ISNOT_EMPTY(idx) (DIGIT(idx))
#define DISALLOWED(idx, digit) ((board[idx]) & DIGIT_STATE(digit))
#define IS_FIXED(idx) (board[idx] & FIXED)

#define DATA_GET_SIZE 1024 // 输入缓冲区大小

#define DecodeURICommentCHAR '#'
#define DecodeURICommentCode1 '%'
#define DecodeURICommentCode2 '2'
#define DecodeURICommentCode3 '3'

#define DecodeURICHAR ';'
#define DecodeURICode1 '%'
#define DecodeURICode2 '3'
#define DecodeURICode3 'B'

static void regular_data_get(char *const data_get, size_t *data_get_size)
{
	char *p, *i, *j, *je;
	for (p = data_get; *p && p < data_get + *data_get_size; p++)
	{
		if (p[0] == DecodeURICode1 && p[1] == DecodeURICode2 && p[2] == DecodeURICode3)
		{
			*p = '\n';
			for (i = p + 1, j = p + 3, je = data_get + *data_get_size; j < je && (*i++ = *j++);)
				;
			if (i[-1] != '\0')
				i[0] = '\0';
			*data_get_size -= 2;
		}
		else if (p[0] == DecodeURICommentCode1 && p[1] == DecodeURICommentCode2 && p[2] == DecodeURICommentCode3)
		{
			*p = DecodeURICommentCHAR;
			for (i = p + 1, j = p + 3, je = data_get + *data_get_size; j < je && (*i++ = *j++);)
				;
			if (i[-1] != '\0')
				i[0] = '\0';
			*data_get_size -= 2;
		}
	}
}

/* 成功返回 0, 失败返回 -1 */
static int dgets(char *string, int n, char **getcp, const char *const getcpe)
{
	if (!n--)
		return -1;
	if (**getcp && n)
	{
		register int i = 0;
		while (i < n && *getcp < getcpe && **getcp && **getcp != '\n')
		{
			*string++ = *(*getcp)++;
			i++;
		}
		if (i == n || **getcp != '\n')
		{
			*string = '\0';
		}
		else
		{
			*string++ = '\n';
			i++;
			*string = '\0';
			(*getcp)++;
		}
		if (i == 0)
			return -1;
		return 0;
	}
	else
	{
		if (!**getcp)
			string[0] = '\0';
		return -1;
	}
}

static int dsprintf(char **gcp, size_t *data_size, const char *format, ...)
{
	int ret;
	va_list ap;
	va_start(ap, format);
	ret = vsnprintf(*gcp, *data_size, format, ap);
	va_end(ap);
	register char *p = *gcp;
	while (*(*gcp))
		(*gcp)++;
	*data_size -= *gcp - p;
	return ret;
}

static void text(char **gcp, size_t *data_size, int *board)
{
	for (int i = 0; i < 81; ++i)
	{
		if (IS_EMPTY(i))
			dsprintf(gcp, data_size, ".");
		else
			dsprintf(gcp, data_size, "%d", GET_DIGIT(board[i]));
		if (8 == COLUMN(i))
			dsprintf(gcp, data_size, "\n");
	}
}

static inline void add_move(int *history, int *idx_history, int idx, int digit, int flag)
{
	history[(*idx_history)++] = SET_INDEX(idx) | SET_DIGIT(digit) | flag;
}

static inline int idx_row(int el, int idx) /* Index within a row */
{
	return INDEX(el, idx);
}

static inline int idx_column(int el, int idx) /* Index within a column */
{
	return INDEX(idx, el);
}

static inline int idx_block(int el, int idx) /* Index within a block */
{
	return INDEX(3 * (el / 3) + idx / 3, 3 * (el % 3) + idx % 3);
}

static void update(int *board, int idx)
{
	const int row = ROW(idx);
	const int col = COLUMN(idx);
	const int block = IDX_BLOCK(row, col);
	const int mask = DIGIT_STATE(DIGIT(idx));

	board[idx] |= STATE_MASK;

	for (int i = 0; i < 9; ++i)
	{
		board[idx_row(row, i)] |= mask;
		board[idx_column(col, i)] |= mask;
		board[idx_block(block, i)] |= mask;
	}
}

#define COUNTSSIZE 9
#define SIZEOFCOUNTS (sizeof(int) * COUNTSSIZE)
#define DIGITSSIZE 9
#define SIZEOFDIGITS (sizeof(int) * DIGITSSIZE)

static int numset(int *counts, int state)
{
	int n = 0;
	for (int i = STATE_SHIFT + 1; i <= STATE_SHIFT + 9; i++)
	{
		if (state & (1 << i))
			n++;
		else
			++counts[i - STATE_SHIFT - 1];
	}
	return n;
}

static void count_set_digits(int *board, int *digits, int *counts, int el, int (*idx_fn)(int, int))
{
	memset(digits, 0x00, SIZEOFDIGITS);
	memset(counts, 0x00, SIZEOFCOUNTS);
	for (int i = 0; i < 9; i++)
		digits[i] = numset(counts, board[(*idx_fn)(el, i)]);
}

/* 成功返回 0, 失败返回 -1 */
static int singles(int *board, int *possible, int *idx_possible, int el, int (*idx_fn)(int, int), int hintcode)
{
	int i;
	int digits[DIGITSSIZE];
	int counts[COUNTSSIZE];
	count_set_digits(board, digits, counts, el, idx_fn);
	for (i = 0; i < 9; i++)
	{
		if (0 == counts[i])
		{
			int nd = 0;
			for (int j = 0; j < 9; j++)
				if (DIGIT((*idx_fn)(el, j)) == i + 1)
				{
					nd++;
					break;
				}
			if (nd == 0) // 发现矛盾, 这个元素不能有这个数字
				break;
		}
		if (1 == counts[i] && *idx_possible < (2 * 81))
		{
			for (int j = 0; j < 9; j++)
			{
				int idx = (*idx_fn)(el, j);
				if (!DISALLOWED(idx, i + 1))
				{
					possible[(*idx_possible)++] = SET_INDEX(idx) | SET_DIGIT(i + 1) | hintcode;
					break;
				}
			}
		}
		if (8 == digits[i] && *idx_possible < (2 * 81))
		{
			int idx = (*idx_fn)(el, i);
			int sta = (STATE_MASK & ~STATE(idx));
			int d = 0;
			for (sta >>= STATE_SHIFT + 1; 0 != sta; sta >>= 1)
				d++;
			// assert(0 < d && d < 10 && !DISALLOWED(idx, d));
			possible[(*idx_possible)++] = SET_INDEX(idx) | SET_DIGIT(d) | hintcode;
		}
	}
	return -(i < 9);
}

/* 返回发现的确定性可移动的步数, 错误时返回 -1 */
static int findmoves(int *board, int *possible)
{
	int idx_possible = 0;
	for (int el = 0; el < 9; el++)
		if (-1 == singles(board, possible, &idx_possible, el, idx_row, HINT_ROW) ||
		    -1 == singles(board, possible, &idx_possible, el, idx_column, HINT_COLUMN) ||
		    -1 == singles(board, possible, &idx_possible, el, idx_block, HINT_BLOCK))
		{
			return -1;
		}
	return idx_possible;
}

static void pairs(int *board, int *digits, int el, int (*idx_fn)(int, int))
{
	int idx, mask;
	for (int i = 0; i < 8; i++)
		if (digits[i] == 7) /* 2 digits unknown */
		{
			idx = (*idx_fn)(el, i);
			for (int j = i + 1; j < 9; j++)
				if (STATE(idx) == STATE((*idx_fn)(el, j)))
				{
					mask = STATE_MASK ^ (STATE_MASK & board[idx]);
					for (int k = 0; k < i; k++)
						board[(*idx_fn)(el, k)] |= mask;
					for (int k = i + 1; k < j; k++)
						board[(*idx_fn)(el, k)] |= mask;
					for (int k = j + 1; k < 9; k++)
						board[(*idx_fn)(el, k)] |= mask;
					digits[j] = -1;
				}
		}
}

static void exmask(int *board, int mask, int block, int el, int (*idx_fn)(int, int))
{
	for (int i = 0; i < 9; i++)
	{
		int idx = (*idx_fn)(el, i);
		if (block != BLOCK(idx) && IS_EMPTY(idx))
			board[idx] |= mask;
	}
}

static void exblock(int *board, int block, int el, int (*idx_fn)(int, int))
{
	int idx, mask = 0;
	for (int i = 0; i < 9; i++)
	{
		idx = idx_block(block, i);
		if (ISNOT_EMPTY(idx))
			mask |= DIGIT_STATE(DIGIT(idx));
	}
	exmask(board, mask ^ STATE_MASK, block, el, idx_fn);
}

static void block(int *board, int el)
{
	int i, idx, row, col;
	for (i = 0; i < 9 && ISNOT_EMPTY(idx = idx_block(el, i)); i++)
		;
	if (i < 9)
	{
		// assert(IS_EMPTY(idx));
		row = ROW(idx);
		col = COLUMN(idx);
		for (i++; i < 9; i++)
		{
			idx = idx_block(el, i);
			if (IS_EMPTY(idx))
			{
				if (ROW(idx) != row)
					row = -1;
				if (COLUMN(idx) != col)
					col = -1;
			}
		}
		if (row >= 0)
			exblock(board, el, row, idx_row);
		if (col >= 0)
			exblock(board, el, col, idx_column);
	}
}

static void common(int *board, int el)
{
	int row, col, mask;
	for (int digit = 1; digit <= 9; digit++)
	{
		mask = DIGIT_STATE(digit);
		row = col = -1;
		for (int i = 0; i < 9; i++)
		{
			int idx = idx_block(el, i);
			if (IS_EMPTY(idx) && 0 == (board[idx] & mask))
			{
				if (row < 0)
					row = ROW(idx);
				else if (row != ROW(idx))
					row = 9; /* Digit appears in multiple rows */

				if (col < 0)
					col = COLUMN(idx);
				else if (col != COLUMN(idx))
					col = 9; /* Digit appears in multiple columns */
			}
		}
		if (-1 != row && row < 9)
			exmask(board, mask, el, row, idx_row);
		if (-1 != col && col < 9)
			exmask(board, mask, el, col, idx_column);
	}
}

static void position2(int *board, int el)
{
	int posn, count;
	int posn_digit[10];
	for (int digit = 1; digit <= 9; digit++)
	{
		int mask = DIGIT_STATE(digit);
		posn_digit[digit] = count = posn = 0;
		for (int i = 0; i < 9; i++)
			if (0 == (mask & board[idx_block(el, i)]))
			{
				count++;
				posn |= DIGIT_STATE(i);
			}
		if (2 == count)
			posn_digit[digit] = posn;
	}
	for (int digit = 1; digit < 9; digit++)
		if (0 != posn_digit[digit])
		{
			for (int digit2 = digit + 1; digit2 <= 9; digit2++)
				if (posn_digit[digit] == posn_digit[digit2])
				{
					int mask = STATE_MASK ^ (DIGIT_STATE(digit) | DIGIT_STATE(digit2));
					int mask2 = DIGIT_STATE(digit);
					for (int i = 0; i < 9; i++)
					{
						int idx = idx_block(el, i);
						if (0 == (mask2 & board[idx])) // assert(0 == (DIGIT_STATE(digit2) & board[idx]));
							board[idx] |= mask;
					}
					posn_digit[digit] = posn_digit[digit2] = 0;
					break;
				}
		}
}

/* 返回发现的确定性可移动的步数, 错误时返回 -1 */
static int allmoves(int *board, int *possible)
{
	int n;

	n = findmoves(board, possible);
	if (n != 0)
		return n;

	for (int i = 0; i < 9; i++)
	{
		int digits[DIGITSSIZE];
		int counts[COUNTSSIZE];
		count_set_digits(board, digits, counts, i, idx_row);
		pairs(board, digits, i, idx_row);

		count_set_digits(board, digits, counts, i, idx_column);
		pairs(board, digits, i, idx_column);

		count_set_digits(board, digits, counts, i, idx_block);
		pairs(board, digits, i, idx_block);
	}
	n = findmoves(board, possible);
	if (n != 0)
		return n;

	for (int i = 0; i < 9; i++)
	{
		block(board, i);
		common(board, i);
		position2(board, i);
	}
	return findmoves(board, possible);
}

/* 成功返回 0, 失败返回 -1 */
static int fill(int *board, int *history, int *idx_history, int idx, int digit, int flag)
{
	// assert(0 != digit);
	if (ISNOT_EMPTY(idx))
		return (DIGIT(idx) == digit) ? 0 : -1;
	if (DISALLOWED(idx, digit))
		return -1;

	board[idx] = SET_DIGIT(digit) | flag;
	update(board, idx);
	add_move(history, idx_history, idx, digit, flag);

	return 0;
}

/* 确定性求解器: 成功时返回 0, 错误时返回 -1 */
static int deterministic(int *board, int *history, int *idx_history, int *possible)
{
	int n;
	n = allmoves(board, possible);
	while (0 < n)
	{
		for (int i = 0; i < n; i++)
		{
			if (-1 == fill(board, history, idx_history,
				       GET_INDEX(possible[i]),
				       GET_DIGIT(possible[i]),
				       GET_HINT(possible[i])))
				return -1;
		}
		n = allmoves(board, possible);
	}
	return n;
}

/* 成功时返回 0, 错误时返回 -1 */
static int reapply(int *board, int *history, int idx_history)
{
	int allok = 0;
	memset(board, 0x00, SIZEOFBOARD);
	for (int i = 0; i < idx_history; i++)
		if (0 != GET_DIGIT(history[i]))
		{
			int idx = GET_INDEX(history[i]);
			int digit = GET_DIGIT(history[i]);
			if (ISNOT_EMPTY(idx) || DISALLOWED(idx, digit))
				allok = -1;
			board[idx] = SET_DIGIT(digit);
			if (history[i] & FIXED)
				board[idx] |= FIXED;
			update(board, idx);
		}
	return allok;
}

static int cmp(const void *e1, const void *e2)
{
	return GET_DIGIT(*(const int *)e2) - GET_DIGIT(*(const int *)e1);
}

/* 成功时返回可选择个数最少的 index, 无可选择时返回 -1, 错误时返回 -2 */
static int choice(int *board, int *possible)
{
	int i, n;
	int counts[COUNTSSIZE];
	for (n = i = 0; i < 81; i++)
		if (IS_EMPTY(i))
		{
			possible[n] = SET_INDEX(i) | SET_DIGIT(numset(counts, board[i]));
			if (9 == GET_DIGIT(possible[n])) // 这个地方不能填数字
				return -2;
			n++;
		}
	if (0 == n)
		return -1;
	qsort(possible, n, sizeof(possible[0]), cmp);
	return GET_INDEX(possible[0]);
}

/* 填入数字时返回填入的数字, 否则返回 -1 */
static int choose(int *board, int *history, int *idx_history, int idx, int digit)
{
	for (; digit <= 9; digit++)
		if (!DISALLOWED(idx, digit))
		{
			board[idx] = SET_DIGIT(digit) | CHOICE;
			update(board, idx);
			add_move(history, idx_history, idx, digit, CHOICE);
			return digit;
		}
	return -1;
}

/* 更换数字时返回更换数字的 index, 否则返回 -1 */
static int backtrack(int *board, int *history, int *idx_history)
{
	for (; 0 < --(*idx_history);)
		if (history[*idx_history] & CHOICE)
		{
			int idx = GET_INDEX(history[*idx_history]);
			int digit = GET_DIGIT(history[*idx_history]) + 1;
			if (0 == reapply(board, history, *idx_history))
				if (-1 != choose(board, history, idx_history, idx, digit))
					return idx;
		}
	return -1;
}

/* 解成功时返回 0, 否则返回 -1 */
static int solve(int *board, int *history, int *idx_history)
{
	int idx = 0;
	int possible[2 * 81]; // singles 中的两个选择可能重复
	for (;;)
	{
		if (0 == deterministic(board, history, idx_history, possible))
		{
			idx = choice(board, possible);
			if (-1 == idx) // 没有可选择的, 数独解决成功
			{
				idx = 0;
				break;
			}
			else if ((idx < 0 || -1 == choose(board, history, idx_history, idx, 1)) &&
				 -1 == backtrack(board, history, idx_history))
			{ // 当前状态的数独板子无解
				idx = -1;
				break;
			}
		}
		else if (-1 == backtrack(board, history, idx_history))
		{ // 当前状态的数独板子无解
			idx = -1;
			break;
		}
	}
	return idx;
}

static void clear_moves(int *board, int *history, int *idx_history)
{
	int i;
	for (i = 0; history[i] & FIXED; i++)
		;
	*idx_history = i;
	reapply(board, history, *idx_history);
}

static void gen_statistics(int *board, int *history, int *idx_history, char **gcp, size_t *data_size)
{
	if (0 != solve(board, history, idx_history))
	{
		dsprintf(gcp, data_size, "此数独没有解\n");
		return;
	}
	else if (-1 != backtrack(board, history, idx_history) && 0 == solve(board, history, idx_history))
	{
		dsprintf(gcp, data_size, "此数独不具有唯一解\n");
		return;
	}
	else
	{
		clear_moves(board, history, idx_history);
		solve(board, history, idx_history);
		text(gcp, data_size, board);
		return;
	}
}

static inline void reset(int *board, int *history, int *idx_history)
{
	memset(board, 0x00, SIZEOFBOARD);
	memset(history, 0x00, SIZEOFHISTORY);
	*idx_history = 0;
}

/* 成功返回 0, 失败返回 -1 */
static int read_board(int *board, int *history, int *idx_history, char **getcp, char *getcpe)
{
	char line[80];
	char *p;

	line[0] = '#';
	while ('#' == line[0])
	{
		if (dgets(line, sizeof(line), getcp, getcpe))
			return -1;
	}

	for (int row = 0; row < 9; row++)
	{
		for (p = line; *p && isspace(*p); p++)
			;
		for (int col = 0; *p && col < 9; col++, p++)
		{
			if (isdigit(*p))
			{
				if (fill(board, history, idx_history, INDEX(row, col), *p - '0', FIXED))
					return -1;
			}
		}
		if (row < 8 && dgets(line, sizeof(line), getcp, getcpe))
			return -1;
	}
	return 0;
}

// 读取数据
static int read_post_data(request_rec *r, char *data_get, size_t *data_get_size)
{
	if (ap_setup_client_block(r, REQUEST_CHUNKED_DECHUNK) != OK)
		return HTTP_BAD_REQUEST;
	if (ap_should_client_block(r))
	{
		*data_get_size = ap_get_client_block(r, data_get, DATA_GET_SIZE);
		return OK;
	}
	else
	{
		*data_get_size = 0;
		return OK;
	}
}

static int solvesudoku_handler(request_rec *r)
{
	int ret;
	if (!r->handler || strcmp(r->handler, "solvesudoku"))
	{
		return DECLINED;
	}
	if (r->method_number != M_POST)
	{
		return HTTP_METHOD_NOT_ALLOWED;
	}

	char data[DATA_GET_SIZE] = {0}; // 输入 / 输出 两用流
	char *gcp = data;		// 输入 / 输出 流两用指针
	char *getcpe;			// 输入流末指针
	size_t data_size = 0;		// 输入流大小 / 输出流剩余大小

	ret = read_post_data(r, data, &data_size); // 读取数据
	if (ret != OK)
	{
		data_size = 0;
		return ret;
	}
	regular_data_get(data, &data_size); // 规则化读取的数据
	getcpe = data + data_size;	  // 置输入流末指针

	ap_set_content_type(r, "text/plain;charset=utf-8");
	if (data_size == 0)
	{
		ap_set_content_length(r, 19);
		ap_rputs("No post data found.", r);
		return OK;
	}

	int board[BOARDSIZE];
	int idx_history;
	int history[HISTORYSIZE];

	reset(board, history, &idx_history);				 // 初始化板
	if (0 == read_board(board, history, &idx_history, &gcp, getcpe)) // 读取板
	{
		memset(data, 0x00, DATA_GET_SIZE);
		gcp = data;							// 将 data 置为输出流
		data_size = DATA_GET_SIZE;					// 初始化输出流剩余大小
		gen_statistics(board, history, &idx_history, &gcp, &data_size); // 解数独
	}
	else // 读取板失败
	{
		return HTTP_BAD_REQUEST;
	}

	ap_set_content_length(r, DATA_GET_SIZE - data_size);
	ap_rputs(data, r);

	return OK;
}

static void solvesudoku_hooks(apr_pool_t *pool)
{
	(void)pool;
	ap_hook_handler(solvesudoku_handler, NULL, NULL, APR_HOOK_MIDDLE);
}

module AP_MODULE_DECLARE_DATA solvesudoku_module = {
    STANDARD20_MODULE_STUFF,
    NULL,	     /* my_dir_conf,        创建每一个目录配置记录 */
    NULL,	     /* my_dir_merge,       合并目录配置记录*/
    NULL,	     /* my_server_conf,     创建服务器配置记录*/
    NULL,	     /* my_server_merge,    融合服务器配置记录*/
    NULL,	     /* my_cmds,            配置指令 */
    solvesudoku_hooks /* my_hooks            在内核中注册的模块函数 */
};
