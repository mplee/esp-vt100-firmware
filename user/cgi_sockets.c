#include <esp8266.h>
#include <httpd.h>
#include <cgiwebsocket.h>

#include "cgi_sockets.h"
#include "uart_driver.h"
#include "screen.h"

#define SOCK_BUF_LEN 2048
static char sock_buff[SOCK_BUF_LEN];

static void notifyTimCb(void *arg) {
	void *data = NULL;

	for (int i = 0; i < 20; i++) {
		httpd_cgi_state cont = screenSerializeToBuffer(sock_buff, SOCK_BUF_LEN, &data);
		int flg = 0;
		if (cont == HTTPD_CGI_MORE) flg |= WEBSOCK_FLAG_MORE;
		if (i > 0) flg |= WEBSOCK_FLAG_CONT;
		cgiWebsockBroadcast(URL_WS_UPDATE, sock_buff, (int) strlen(sock_buff), flg);
		if (cont == HTTPD_CGI_DONE) break;
	}

	screenSerializeToBuffer(NULL, SOCK_BUF_LEN, &data);
}

static ETSTimer notifyTim;

/**
 * Broadcast screen state to sockets.
 * This is a callback for the Screen module,
 * called after each visible screen modification.
 */
void ICACHE_FLASH_ATTR screen_notifyChange(void)
{
	os_timer_disarm(&notifyTim);
	os_timer_setfn(&notifyTim, notifyTimCb, NULL);
	os_timer_arm(&notifyTim, 20, 0);
}

/** Socket received a message */
void ICACHE_FLASH_ATTR updateSockRx(Websock *ws, char *data, int len, int flags)
{
	char buf[20];
	// Add terminator if missing (seems to randomly happen)
	data[len] = 0;

	dbg("Sock RX str: %s, len %d", data, len);

	if (strstarts(data, "STR:")) {
		// pass string verbatim
		UART_WriteString(UART0, data+4, UART_TIMEOUT_US);
	}
	else if (strstarts(data, "BTN:")) {
		// send button as low ASCII value 1-9
		int btnNum = data[4] - '0';
		if (btnNum > 0 && btnNum < 10) {
			UART_WriteChar(UART0, (unsigned char)btnNum, UART_TIMEOUT_US);
		}
	}
	else if (strstarts(data, "TAP:")) {
		// this comes in as 0-based

		int y=0, x=0;

		char *pc=data+4;
		char c;
		int phase=0;

		while((c=*pc++) != '\0') {
			if (c==','||c==';') {
				phase++;
			}
			else if (c>='0' && c<='9') {
				if (phase==0) {
					y=y*10+(c-'0');
				} else {
					x=x*10+(c-'0');
				}
			}
		}

		if (!screen_isCoordValid(y, x)) {
			warn("Mouse input at invalid coordinates");
			return;
		}

		dbg("Screen clicked at row %d, col %d", y+1, x+1);

		// Send as 1-based to user
		sprintf(buf, "\033[%d;%dM", y+1, x+1);
		UART_WriteString(UART0, buf, UART_TIMEOUT_US);
	}
	else {
		warn("Bad command.");
	}
}

/** Socket connected for updates */
void ICACHE_FLASH_ATTR updateSockConnect(Websock *ws)
{
	info("Socket connected to "URL_WS_UPDATE);
	ws->recvCb = updateSockRx;
}
