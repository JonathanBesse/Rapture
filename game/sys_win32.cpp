#include "sys_local.h"
#include <direct.h>
#include <process.h>

char* Sys_FS_GetHomepath() {
	// not sure
	return NULL;
}

char* Sys_FS_GetBasepath() {
	static char cwd[1024];
	_getcwd(cwd, 1023);
	cwd[1023] = '\0';
	return cwd;
}

void Sys_RunThread(void (*threadRun)(void*), void* arg) {
	_beginthread(threadRun, 0, arg);
}

// http://stackoverflow.com/questions/14762456/getclipboarddatacf-text
string Sys_GetClipboardContents() {
	if(!OpenClipboard(nullptr))
		return "";

	// Get handle of clipboard object for ANSI text
	HANDLE hData = GetClipboardData(CF_TEXT);
	if (hData == nullptr)
		return ""; // error

	// Lock the handle to get the actual text pointer
	char * pszText = static_cast<char*>( GlobalLock(hData) );
	if (pszText == nullptr)
		return ""; // error

	// Save text in a string class instance
	std::string text( pszText );

	// Release the lock
	GlobalUnlock( hData );

	// Release the clipboard
	CloseClipboard();

	return text;
}

void Sys_SendToClipboard(string text) {
	if(text.length() <= 0) {
		OpenClipboard(0);
		EmptyClipboard();
		CloseClipboard();
		return;
	}
	HGLOBAL hMem =  GlobalAlloc(GMEM_MOVEABLE, text.length());
	memcpy(GlobalLock(hMem), text.c_str(), text.length());
	GlobalUnlock(hMem);
	OpenClipboard(0);
	EmptyClipboard();
	SetClipboardData(CF_TEXT, hMem);
	CloseClipboard();
}

