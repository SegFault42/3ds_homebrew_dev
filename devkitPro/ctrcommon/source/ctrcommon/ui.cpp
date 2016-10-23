#include "ctrcommon/ui.hpp"

#include "ctrcommon/input.hpp"
#include "ctrcommon/platform.hpp"
#include "ctrcommon/socket.hpp"

#include <arpa/inet.h>
#include <sys/dirent.h>
#include <sys/errno.h>
#include <sys/unistd.h>
#include <string.h>

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stack>

struct uiAlphabetize {
    inline bool operator()(SelectableElement a, SelectableElement b) {
        return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
    }
};

bool uiSelect(SelectableElement* selected, std::vector<SelectableElement> elements, std::function<bool(std::vector<SelectableElement> &currElements, bool &elementsDirty, bool &resetCursorIfDirty)> onLoop, std::function<bool(SelectableElement select)> onSelect, bool useTopScreen, bool alphabetize) {
    u32 cursor = 0;
    u32 scroll = 0;

    u32 selectionScroll = 0;
    u64 selectionScrollEndTime = 0;

    u64 lastScrollTime = 0;

    bool elementsDirty = false;
    bool resetCursorIfDirty = true;
    if(alphabetize) {
        std::sort(elements.begin(), elements.end(), uiAlphabetize());
    }

    while(platformIsRunning()) {
        inputPoll();
        if(inputIsPressed(BUTTON_A)) {
            SelectableElement select = elements.at(cursor);
            if(onSelect == NULL || onSelect(select)) {
                *selected = select;
                return true;
            }
        }

        if(inputIsHeld(BUTTON_DOWN) || inputIsHeld(BUTTON_UP)) {
            if(lastScrollTime == 0 || platformGetTime() - lastScrollTime >= 180) {
                if(inputIsHeld(BUTTON_DOWN) && cursor < elements.size() - 1) {
                    cursor++;
                    if(cursor >= scroll + 20) {
                        scroll++;
                    }

                    selectionScroll = 0;
                    selectionScrollEndTime = 0;

                    lastScrollTime = platformGetTime();
                }

                if(inputIsHeld(BUTTON_UP) && cursor > 0) {
                    cursor--;
                    if(cursor < scroll) {
                        scroll--;
                    }

                    selectionScroll = 0;
                    selectionScrollEndTime = 0;

                    lastScrollTime = platformGetTime();
                }
            }
        } else {
            lastScrollTime = 0;
        }

        screenBeginDraw(BOTTOM_SCREEN);
        screenClear(0, 0, 0);

        u16 screenWidth = screenGetWidth();
        for(std::vector<SelectableElement>::iterator it = elements.begin() + scroll; it != elements.begin() + scroll + 20 && it != elements.end(); it++) {
            SelectableElement element = *it;
            u32 index = (u32) (it - elements.begin());
            u8 color = 255;
            int offset = 0;
            if(index == cursor) {
                color = 0;
                screenFill(0, (int) (index - scroll) * 12 - 2, screenWidth, (u16) (screenGetStrHeight(element.name) + 4), 255, 255, 255);
                u32 width = (u32) screenGetStrWidth(element.name);
                if(width > screenWidth) {
                    if(selectionScroll + screenWidth >= width) {
                        if(selectionScrollEndTime == 0) {
                            selectionScrollEndTime = platformGetTime();
                        } else if(platformGetTime() - selectionScrollEndTime >= 4000) {
                            selectionScroll = 0;
                            selectionScrollEndTime = 0;
                        }
                    } else {
                        selectionScroll++;
                    }
                }

                offset = -selectionScroll;
            }

            screenDrawString(element.name, offset, (int) (index - scroll) * 12, color, color, color);
        }

        screenEndDraw();

        if(useTopScreen) {
            screenBeginDraw(TOP_SCREEN);
            screenClear(0, 0, 0);

            SelectableElement currSelected = elements.at(cursor);
            if(currSelected.details.size() != 0) {
                std::stringstream details;
                for(std::vector<std::string>::iterator it = currSelected.details.begin(); it != currSelected.details.end(); it++) {
                    details << *it << "\n";
                }

                screenDrawString(details.str(), 0, 0, 255, 255, 255);
            }
        }

        bool result = onLoop != NULL && onLoop(elements, elementsDirty, resetCursorIfDirty);
        if(elementsDirty) {
            if(resetCursorIfDirty) {
                cursor = 0;
                scroll = 0;
            }

            selectionScroll = 0;
            selectionScrollEndTime = 0;
            if(alphabetize) {
                std::sort(elements.begin(), elements.end(), uiAlphabetize());
            }

            elementsDirty = false;
            resetCursorIfDirty = true;
        }

        if(useTopScreen) {
            screenEndDraw();
        }

        screenSwapBuffers();
        if(result) {
            break;
        }
    }

    return false;
}

void uiGetDirContents(std::vector<SelectableElement> &elements, const std::string directory, std::vector<std::string> extensions) {
    elements.clear();
    elements.push_back({".", "."});
    elements.push_back({"..", ".."});

    bool hasSlash = directory.size() != 0 && directory[directory.size() - 1] == '/';
    const std::string dirWithSlash = hasSlash ? directory : directory + "/";

    DIR* dir = opendir(dirWithSlash.c_str());
    if(dir == NULL) {
        return;
    }

    while(true) {
        struct dirent* ent = readdir(dir);
        if(ent == NULL) {
            break;
        }

        const std::string dirName = std::string(ent->d_name);
        const std::string path = dirWithSlash + dirName;
        if(fsIsDirectory(path)) {
            elements.push_back({path, dirName});
        } else {
            std::string::size_type dotPos = path.rfind('.');
            if(extensions.empty() || (dotPos != std::string::npos && std::find(extensions.begin(), extensions.end(), path.substr(dotPos + 1)) != extensions.end())) {
                struct stat st;
                stat(path.c_str(), &st);

                std::vector<std::string> info;
                std::stringstream stream;
                stream << "File Size: " << st.st_size << " bytes (" << std::fixed << std::setprecision(2) << st.st_size / 1024.0f / 1024.0f << "MB)";
                info.push_back(stream.str());
                elements.push_back({path, dirName, info});
            }
        }
    }

    closedir(dir);
}

bool uiSelectFile(std::string* selectedFile, const std::string rootDirectory, std::vector<std::string> extensions, std::function<bool(bool inRoot)> onLoop, bool useTopScreen) {
    std::stack<std::string> directoryStack;
    std::string currDirectory = rootDirectory;

    std::vector<SelectableElement> elements;
    uiGetDirContents(elements, currDirectory, extensions);

    bool changeDirectory = false;
    SelectableElement selected;
    bool result = uiSelect(&selected, elements, [&](std::vector<SelectableElement> &currElements, bool &elementsDirty, bool &resetCursorIfDirty) {
        if(onLoop != NULL && onLoop(directoryStack.empty())) {
            return true;
        }

        if(inputIsPressed(BUTTON_B) && !directoryStack.empty()) {
            currDirectory = directoryStack.top();
            directoryStack.pop();
            changeDirectory = true;
        }

        if(changeDirectory) {
            uiGetDirContents(currElements, currDirectory, extensions);
            elementsDirty = true;
            changeDirectory = false;
        }

        return false;
    }, [&](SelectableElement select) {
        if(select.name.compare(".") == 0) {
            return false;
        } else if(select.name.compare("..") == 0) {
            if(!directoryStack.empty()) {
                currDirectory = directoryStack.top();
                directoryStack.pop();
                changeDirectory = true;
            }

            return false;
        } else if(fsIsDirectory(select.id)) {
            directoryStack.push(currDirectory);
            currDirectory = select.id;
            changeDirectory = true;
            return false;
        }

        return true;
    }, useTopScreen, true);

    if(result) {
        *selectedFile = selected.id;
    }

    return result;
}

bool uiSelectApp(App* selectedApp, MediaType mediaType, std::function<bool()> onLoop, bool useTopScreen) {
    std::vector<App> apps = appList(mediaType);
    std::vector<SelectableElement> elements;
    for(std::vector<App>::iterator it = apps.begin(); it != apps.end(); it++) {
        App app = *it;

        std::stringstream titleId;
        titleId << std::setfill('0') << std::setw(16) << std::hex << app.titleId;

        std::stringstream uniqueId;
        uniqueId << std::setfill('0') << std::setw(8) << std::hex << app.uniqueId;

        std::vector<std::string> details;
        details.push_back("Title ID: " + titleId.str());
        details.push_back("Unique ID: " + uniqueId.str());
        details.push_back("Product Code: " + std::string(app.productCode));
        details.push_back("Platform: " + appGetPlatformName(app.platform));
        details.push_back("Category: " + appGetCategoryName(app.category));

        elements.push_back({titleId.str(), app.productCode, details});
    }

    if(elements.size() == 0) {
        elements.push_back({"None", "None"});
    }

    SelectableElement selected;
    bool result = uiSelect(&selected, elements, [&](std::vector<SelectableElement> &currElements, bool &elementsDirty, bool &resetCursorIfDirty) {
        return onLoop != NULL && onLoop();
    }, [&](SelectableElement select) {
        return select.name.compare("None") != 0;
    }, useTopScreen, true);

    if(result) {
        for(std::vector<App>::iterator it = apps.begin(); it != apps.end(); it++) {
            App app = *it;
            if(app.titleId == (u64) strtoll(selected.id.c_str(), NULL, 16)) {
                *selectedApp = app;
            }
        }
    }

    return result;
}

void uiDisplayMessage(Screen screen, const std::string message) {
    screenBeginDraw(screen);
    screenClear(0, 0, 0);
    screenDrawString(message, (screenGetWidth() - screenGetStrWidth(message)) / 2, (screenGetHeight() - screenGetStrHeight(message)) / 2, 255, 255, 255);
    screenEndDraw();
    screenSwapBuffers();
}

bool uiPrompt(Screen screen, const std::string message, bool question) {
    std::stringstream stream;
    stream << message << "\n";
    if(question) {
        stream << "Press A to confirm, B to cancel." << "\n";
    } else {
        stream << "Press Start to continue." << "\n";
    }

    std::string str = stream.str();
    while(platformIsRunning()) {
        inputPoll();
        if(question) {
            if(inputIsPressed(BUTTON_A)) {
                return true;
            }

            if(inputIsPressed(BUTTON_B)) {
                return false;
            }
        } else {
            if(inputIsPressed(BUTTON_START)) {
                return true;
            }
        }

        uiDisplayMessage(screen, str);
    }

    return false;
}

void uiDisplayProgress(Screen screen, const std::string operation, const std::string details, bool quickSwap, int progress) {
    std::stringstream stream;
    stream << operation << ": [";
    int progressBars = progress / 4;
    for(int i = 0; i < 25; i++) {
        if(i < progressBars) {
            stream << '|';
        } else {
            stream << ' ';
        }
    }

    std::ios state(NULL);
    state.copyfmt(stream);
    stream << "] " << std::setfill(' ') << std::setw(3) << progress;
    stream.copyfmt(state);
    stream << "%" << "\n";
    stream << details << "\n";

    std::string str = stream.str();

    screenBeginDraw(screen);
    screenClear(0, 0, 0);
    screenDrawString(str, (screenGetWidth() - screenGetStrWidth(str)) / 2, (screenGetHeight() - screenGetStrHeight(str)) / 2, 255, 255, 255);
    screenEndDraw();
    if(quickSwap) {
        screenSwapBuffersQuick();
    } else {
        screenSwapBuffers();
    }
}

RemoteFile uiAcceptRemoteFile(Screen screen) {
    uiDisplayMessage(screen, "Initializing...");

    int listen = socketListen(5000);
    if(listen < 0) {
        std::stringstream errStream;
        errStream << "Failed to initialize." << "\n" << strerror(errno) << "\n";
        uiPrompt(screen, errStream.str(), false);
        return {NULL, 0};
    }

    std::stringstream waitStream;
    waitStream << "Waiting for peer to connect..." << "\n";
    waitStream << "IP: " << inet_ntoa({socketGetHostIP()}) << "\n";
    waitStream << "Press B to cancel." << "\n";
    uiDisplayMessage(screen, waitStream.str());

    FILE* socket;
    while((socket = socketAccept(listen)) == NULL) {
        if(errno != EAGAIN && errno != EWOULDBLOCK && errno != EINPROGRESS) {
            close(listen);

            std::stringstream errStream;
            errStream << "Failed to accept peer." << "\n" << strerror(errno) << "\n";
            uiPrompt(screen, errStream.str(), false);
            return {NULL, 0};
        } else if(platformIsRunning()) {
            inputPoll();
            if(inputIsPressed(BUTTON_B)) {
                close(listen);
                return {NULL, 0};
            }
        } else {
            return {NULL, 0};
        }
    }

    close(listen);

    uiDisplayMessage(screen, "Reading info...");

    u64 fileSize;
    u64 bytesRead = 0;
    while(platformIsRunning()) {
        u64 currBytesRead = fread(&fileSize, 1, (size_t) (sizeof(fileSize) - bytesRead), socket);
        bytesRead += currBytesRead;
        if(currBytesRead == 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            if(bytesRead != sizeof(fileSize)) {
                fclose(socket);

                std::stringstream errStream;
                errStream << "Failed to read info." << "\n" << strerror(errno) << "\n";
                uiPrompt(screen, errStream.str(), false);
                return {NULL, 0};
            }

            break;
        }
    }

    if(!platformIsRunning()) {
        return {NULL, 0};
    }

    fileSize = ntohll(fileSize);
    return {socket, fileSize};
}