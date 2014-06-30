#include "Client.h"
#include "Worldspace.h"
#include "DungeonManager.h"

Client* thisClient = nullptr;

Client::Client() {
	ptConsolasFont = trap->RegisterFont("fonts/consola.ttf", 18);
	framecount = 0;
	framelast = 0;
	frametime = 0;

	memset(frametimes, 0, sizeof(frametimes));
	framecount = 0;
	framelast = trap->GetTicks();
	fps = 0;

	ptHUD = trap->RegisterStaticMenu("ui/hud.html");
	bShouldDrawLabels = false;
	bStoppedDrawingLabels = false;
}

Client::~Client() {
}

void Client::RunFPS() {
	unsigned int index = framecount % FRAME_CAPTURE;
	unsigned int ticks = trap->GetTicks();
	unsigned int count = 0;

	frametimes[index] = ticks - framelast;
	framecount++;
	
	if(framecount < FRAME_CAPTURE) {
		count = framecount;
	} else {
		count = FRAME_CAPTURE;
	}

	framelast = ticks;

	fps = 0;
	for(int i = 0; i < count; i++) {
		fps += frametimes[i];
	}

	fps /= count;
	fps = 1000 / fps;

	frametime = frametimes[index];
}

void Client::DrawViewportInfo() {
	int drawFPS = 0;
	bool drawXY = false;
	bool drawWorldXY = false;
	stringstream ss;

	trap->CvarIntVal("cg_drawfps", &drawFPS);
	trap->CvarBoolVal("cg_drawxy", &drawXY);
	trap->CvarBoolVal("cg_drawworldxy", &drawWorldXY);

	if(drawFPS >= 1) {
		if(drawFPS == 1 || drawFPS == 3) {
			ss << "FPS: ";
			ss << fps;
			ss << "      ";
		}
		if(drawFPS == 2 || drawFPS == 3) {
			ss << "ms: ";
			ss << frametime;
			ss << "      ";
		}
	}

	if(drawXY) {
		ss << "Screen Space: ";
		ss << cursorX;
		ss << "X / ";
		ss << cursorY;
		ss << "Y      ";
	}

	if(drawWorldXY) {
		ss << "World Space: ";
		ss << (Worldspace::ScreenSpaceToWorldPlaceX(cursorX, cursorY, ptDungeonManager->GetWorld(0)->GetFirstPlayer()));
		ss << "X / ";
		ss << (Worldspace::ScreenSpaceToWorldPlaceY(cursorX, cursorY, ptDungeonManager->GetWorld(0)->GetFirstPlayer()));
		ss << "Y      ";
	}
	if(ss.str().length() > 0) {
		trap->RenderTextShaded(ptConsolasFont, ss.str().c_str(), 255, 255, 255, 127, 127, 127);
	}
}

void Client::Frame() {
	bShouldDrawLabels = false;
	RunFPS();
	DrawViewportInfo();

	ptDungeonManager->GetWorld(ptPlayer->iAct)->Render(this);
	if(!bShouldDrawLabels && !bStoppedDrawingLabels) {
		HideLabels();
	}
}

void Client::HideLabels() {
	trap->RunJavaScript(ptHUD, "stopLabelDraw();");
	bStoppedDrawingLabels = true;
}

void Client::StartLabelDraw(const char* sLabel) {
	stringstream fullName;
	fullName << "startLabelDraw(\"" << sLabel << "\", " << cursorX+15 << ", " << cursorY-30 << ");";
	trap->RunJavaScript(ptHUD, fullName.str().c_str());
	bStoppedDrawingLabels = false;
}

void Client::EnteredArea(const char* sArea) {
	stringstream fullName;
	fullName << "changeDivHTML('ID_zone', \"" << sArea << "\");";
	trap->RunJavaScript(ptHUD, fullName.str().c_str());
}

void Client::PassMouseDown(int x, int y) {
	cursorX = x;
	cursorY = y;
	if(!trap->IsConsoleOpen()) {
		ptPlayer->MouseDownEvent(x, y);
	}
}

void Client::PassMouseUp(int x, int y) {
	cursorX = x;
	cursorY = y;
	ptPlayer->MouseUpEvent(x, y);
}

void Client::PassMouseMove(int x, int y) {
	cursorX = x;
	cursorY = y;
	if(!trap->IsConsoleOpen()) {
		ptPlayer->MouseMoveEvent(x, y);
	}
}