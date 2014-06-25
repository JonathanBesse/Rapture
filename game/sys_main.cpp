#include "sys_local.h"

/* main game initialization */

static RaptureGame *sys = NULL;
bool bStartupQuit = false;

void setGameQuitting(const bool b) { if(!sys) bStartupQuit = b; else sys->bHasFinished = true; }

int main(int argc, char** argv) {
	try {
		sys = new RaptureGame(argc, argv);
		FrameCapper fc;
		while(!sys->bHasFinished) {
			fc.StartFrame();
			sys->RunLoop();
			fc.EndFrame();
		}
	}
	catch(const bool) {
		while((sys && !sys->bHasFinished) || (!bStartupQuit)) {
#ifdef _WIN32
			MSG        msg;
			if (!GetMessage (&msg, NULL, 0, 0)) {
				break;
			}
			TranslateMessage(&msg);
			DispatchMessage(&msg);
#endif
		}
	}
	if(sys) {
		delete sys;
	}
	return 0;
}

// This accounts for all input
int RaptureInputCallback(void *notUsed, SDL_Event* e) {
	if(sys->bHasFinished) {
		// DONT send input...
		return 1;
	}
	switch(e->type) {
		case SDL_APP_TERMINATING:
		case SDL_QUIT:
			sys->PassQuitEvent();
			break;
		case SDL_KEYDOWN:
			Input->SendKeyDownEvent(e->key.keysym, e->text.text);
			break;
		case SDL_KEYUP:
			Input->SendKeyUpEvent(e->key.keysym, e->text.text);
			break;
		case SDL_MOUSEMOTION:
			Input->SendMouseMoveEvent(e->motion.x, e->motion.y);
			break;
		case SDL_MOUSEBUTTONUP:
		case SDL_MOUSEBUTTONDOWN:
			Input->SendMouseButtonEvent(e->button.button, e->button.state, e->button.x, e->button.y);
			break;
		case SDL_JOYAXISMOTION:
			break;
		case SDL_TEXTINPUT:
			Input->SendTextInputEvent(e->text.text);
			break;
		case SDL_TEXTEDITING:
			// Not used?
			break;
		case SDL_JOYBALLMOTION:
			break;
		case SDL_JOYHATMOTION:
			break;
		case SDL_JOYBUTTONDOWN:
			break;
		case SDL_JOYBUTTONUP:
			break;
		case SDL_CONTROLLERAXISMOTION:
			break;
		case SDL_CONTROLLERBUTTONDOWN:
			break;
		case SDL_CONTROLLERBUTTONUP:
			break;
	}
	return 1;
}

/* RaptureGame class, this does all the heavy lifting */
RaptureGame::RaptureGame(int argc, char **argv) : bHasFinished(false) {
	game = NULL;
	editor = NULL;

	Sys_InitViewlog();

	// init cmds
	Sys_InitCommands();

	// Init zone memory
	Zone::Init();

	// Init the cvar system (so we can assign preliminary cvars in the commandline)
	CvarSystem::Initialize();

	// Init filesystem
	FileSystem::Init();

	// Read from the config
	Cmd::ProcessCommand("exec raptureconfig.cfg");

	// Actually read the arguments
	HandleCommandline(argc, argv);

	// Init hunk memory

	// Init the renderer
	RenderCode::Initialize();

	// Init rand (cuz we will DEFINITELY need it ;D

	// Init input
	SDL_SetEventFilter(RaptureInputCallback, NULL);
	InitInput();

	// Load font
	FontMan = new FontManager();

	// Init UI
	UI::Initialize();
}

/* Called after the main loop has finished and we are ready to shut down */
RaptureGame::~RaptureGame() {
	if(game) {
		trap->shutdown();
		delete game;
	}

	delete FontMan;
	DeleteInput();
	RenderCode::Exit();
	CvarSystem::Destroy();
	FileSystem::Shutdown();
	Zone::Shutdown();
}

/* Run every frame */
void RaptureGame::RunLoop() {

	// Do input
	Input->InputFrame();
	UI::Update();

	RenderCode::BlankFrame();

	// Do gamecode
	if(game || editor) {
		trap->runactiveframe();
	}

	// Do rendering
	UI::Render();
	RenderCode::Display();
}

/* Deal with the commandline arguments */
void RaptureGame::HandleCommandline(int argc, char** argv) {
	if(argc <= 1) return;
	string ss = "";
	for(int i = 1; i < argc; i++) {
		ss += argv[i];
	}
	vector<string> s;
	split(ss, '+', s);
	for(auto it = s.begin(); it != s.end(); ++it) {
		Cmd::ProcessCommand(it->c_str());
	}
}

/* Quit the game. */
void RaptureGame::PassQuitEvent() {
	bHasFinished = true;
}

/* Set up common exports which are used in both the game and the editor */
void RaptureGame::AssignExports(gameImports_s *imp) {
	imp->printf = R_Printf;
	imp->error = R_Error;
	imp->GetTicks = reinterpret_cast<int(*)()>(SDL_GetTicks);
	imp->OpenFile = FileSystem::EXPORT_OpenFile;
	imp->CloseFile = FileSystem::EXPORT_Close;
	imp->GetFileSize = FileSystem::EXPORT_GetFileSize;
	imp->ListFilesInDir = FileSystem::EXPORT_ListFilesInDir;
	imp->FreeFileList = FileSystem::FreeFileList;
	imp->ReadPlaintext = FileSystem::EXPORT_ReadPlaintext;
	imp->ReadBinary = FileSystem::EXPORT_ReadBinary;
	imp->RegisterImage = RenderCode::RegisterImage;
	imp->DrawImage = RenderCode::DrawImage;
	imp->DrawImageAbs = static_cast<void(*)(Image*, int, int, int, int)>(RenderCode::DrawImageAbs);
	imp->DrawImageAspectCorrection = RenderCode::DrawImageAspectCorrection;
	imp->DrawImageClipped = RenderCode::DrawImageClipped;
	imp->InitMaterials = RenderCode::InitMaterials;
	imp->ShutdownMaterials = RenderCode::ShutdownMaterials;
	imp->RegisterMaterial = RenderCode::RegisterMaterial;
	imp->RenderMaterial = RenderCode::SendMaterialToRenderer;
	imp->RenderMaterialTrans = RenderCode::SendMaterialToRendererTrans;
	imp->CvarBoolVal = CvarSystem::EXPORT_BoolValue;
	imp->CvarIntVal = CvarSystem::EXPORT_IntValue;
	imp->CvarStrVal = CvarSystem::EXPORT_StrValue;
	imp->CvarValue = CvarSystem::EXPORT_Value;
	imp->RegisterCvarBool = static_cast<Cvar*(*)(const string&, const string&, int, bool)>(CvarSystem::RegisterCvar);
	imp->RegisterCvarFloat = static_cast<Cvar*(*)(const string&, const string&, int, float)>(CvarSystem::RegisterCvar);
	imp->RegisterCvarInt = static_cast<Cvar*(*)(const string&, const string&, int, int)>(CvarSystem::RegisterCvar);
	imp->RegisterCvarStr = static_cast<Cvar*(*)(const string&, const string&, int, char*)>(CvarSystem::RegisterCvar);
	imp->RegisterFont = Font::Register;
	imp->RenderTextBlended = RenderCode::RenderTextBlended;
	imp->RenderTextShaded = RenderCode::RenderTextShaded;
	imp->RenderTextSolid = RenderCode::RenderTextSolid;
	imp->RegisterStaticMenu = UI::RegisterStaticMenu;
	imp->KillStaticMenu = UI::KillStaticMenu;
	imp->RunJavaScript = UI::RunJavaScript;
	imp->Zone_Alloc = Zone::VMAlloc;
	imp->Zone_FastFree = Zone::VMFastFree;
	imp->Zone_Free = Zone::Free;
	imp->Zone_FreeAll = Zone::VMFreeAll;
	imp->Zone_NewTag = Zone::NewTag;
	imp->Zone_Realloc = Zone::Realloc;
}

/* Started a new game (probably from the menu) */
GameModule* RaptureGame::CreateGameModule(const char* bundle) {
	GameModule* game = new GameModule(bundle);
	static gameImports_s imp;
	AssignExports(&imp);
	
	trap = game->GetRefAPI(&imp);
	if(!trap) {
		return NULL;
	}
	trap->init();
	return game;
}

/* Get the game module */
GameModule* RaptureGame::GetGameModule() {
	if(sys) {
		return sys->game;
	}
	return NULL;
}

gameExports_s* RaptureGame::GetImport() {
	if(sys) {
		return sys->trap;
	}
	return NULL;
}

/* Some shared functions */
#include "ui_local.h"
void R_Printf(const char *fmt, ...) {
	va_list args;
	char str[1024];

	va_start(args, fmt);
	vsnprintf(str, 1024, fmt, args);
	va_end(args);

	Console::PushConsoleMessage(str);
	Sys_PassToViewlog(str);
}

void NewGame() {
	MainMenu::DestroySingleton();
	if(Console::GetSingleton()->IsOpen()) {
		Console::GetSingleton()->Hide();
	}
	if(sys->game || sys->editor) {
		// Have to exit from these first
		return;
	}
	sys->game = sys->CreateGameModule("gamex86");
}

void StartEditor() {
	if(Console::GetSingleton()->IsOpen()) {
		Console::GetSingleton()->Hide();
	}
	MainMenu::DestroySingleton();
	if(sys->game || sys->editor) {
		// Have to exit from these first
		return;
	}
	sys->editor = sys->CreateGameModule("editorx86");
}

void ReturnToMain() {
	if(Console::GetSingleton()->IsOpen()) {
		Console::GetSingleton()->Hide();
	}
	if(sys->game) {
		sys->trap->shutdown();
		delete sys->game;
		sys->game = NULL;
	} else if(sys->editor) {
		sys->trap->shutdown();
		delete sys->editor;
		sys->editor = NULL;
	}
	R_Printf("creating main menu webview\n");
	MainMenu::GetSingleton();
}