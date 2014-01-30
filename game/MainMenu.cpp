#include "ui_local.h"
/* Namespace management */
using namespace Awesomium;

MainMenu* MainMenu::singleton = NULL;

MainMenu::MainMenu() {
	R_Printf("Loading main menu...");
	string mainFileName = "file://" + File::GetFileSearchPath("ui/main.html");
	wView = wc->CreateWebView(r_width->Integer(), r_height->Integer());
	// TODO: refactor all of this into a inherited func
	wView->LoadURL(WebURL(WSLit(mainFileName.c_str())));
	while(wView->IsLoading())
		wc->Update();
	R_Printf("done.\n");
	AddRenderable(wView);

	global = wView->CreateGlobalJavascriptObject(WSLit("GameManager"));
	wView->set_js_method_handler(this);
	JSObject jObj = global.ToObject();
	SetupBaseCommands(&jObj);
}

MainMenu::~MainMenu() {
	wView->Destroy();
	wView = NULL;
}

MainMenu::MainMenu(MainMenu& other) {
	// Shouldn't ever happen since we're using singleton construct
	wView = other.wView;

}

MainMenu& MainMenu::operator=(MainMenu& other) {
	// Shouldn't ever happen since we're using singleton construct
	wView = other.wView;
	return (*this);
}

void MainMenu::OnMethodCall(WebView* caller, unsigned int remote_caller_id,
		const WebString& method_name, const JSArray& args) {
	if(!ExecuteBaseCommand(ToString(method_name), args))
		return;
	// Check for our command
}

JSValue MainMenu::OnMethodCallWithReturnValue(WebView* caller, unsigned int remote_caller_id,
		const WebString& method_name, const JSArray& args) {
	auto x = ExecuteBaseCommandWithReturn(ToString(method_name), args);
	if(x.first)
		return x.second;
	// Check for our command
}