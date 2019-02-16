COMP=apxs
COMPFLAGS=-S CFLAGS='-prefer-pic -pipe -Wall -Wextra -s -O6 -fstack-protector-strong -Wformat -Werror=format-security -Wdate-time -D_FORTIFY_SOURCE=2 -DLINUX -D_REENTRANT -D_GNU_SOURCE -pthread'

default:
	@echo
	@echo '       make all          # 编译模块'
	@echo '       make install      # 安装模块'
	@echo '       make uninstall    # 卸载模块'
	@echo '       make clean-all    # 做清理工作'
	@echo
	@echo '       make rebuild      # 重新编译、安装模块'
	@echo

rebuild:
	@$(MAKE) clean-all ;$(MAKE) all ;sudo $(MAKE) uninstall ;sudo $(MAKE) install

all: mod_solvesudoku.la
mod_solvesudoku.la: mod_solvesudoku.c
	$(COMP) $(COMPFLAGS) -c $^
install: mod_solvesudoku.la
	$(COMP) -i $^
uninstall:
	rm -f /usr/lib/apache2/modules/mod_solvesudoku.so
	@echo '请删除 /etc/apache2/apache2.conf 中的以下内容:'
	@echo
	@echo '       #   apache2.conf'
	@echo '       LoadModule solvesudoku_module /usr/lib/apache2/modules/mod_solvesudoku.so'
	@echo '       <Location /solvesudoku>'
	@echo '       SetHandler solvesudoku'
	@echo '       </Location>'
	@echo
clean-all:
	-rm -r ./.libs
	-rm ./*.la ./*.lo ./*.slo
