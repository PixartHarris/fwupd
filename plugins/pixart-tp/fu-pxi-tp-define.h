#pragma once
#include <fwupdplugin.h>

/* 如果已經有錯就 prefix；否則新建一個錯誤 */
#define PXI_FAIL(_err, _domain, _code, ...)                                                        \
	G_STMT_START                                                                               \
	{                                                                                          \
		if ((_err) != NULL) {                                                              \
			if (*(_err) != NULL)                                                       \
				g_prefix_error((_err), __VA_ARGS__);                               \
			else                                                                       \
				g_set_error((_err), (_domain), (_code), __VA_ARGS__);              \
		}                                                                                  \
	}                                                                                          \
	G_STMT_END

// /* 安全取得錯誤字串（不會因為 *error==NULL 而崩） */
// #define PXI_ERRMSG(_err) (((_err) && *(_err)) ? (*(_err))->message : "unknown")
