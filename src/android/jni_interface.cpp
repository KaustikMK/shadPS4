#include <jni.h>
#include <filesystem>
#include "emulator.h"

extern "C" JNIEXPORT jint JNICALL
Java_net_shadps4_shadps4_SharedLib_runGame(JNIEnv* env, jobject /*thiz*/, jstring jpath) {
    const char* path_chars = env->GetStringUTFChars(jpath, nullptr);
    std::filesystem::path path{path_chars};
    env->ReleaseStringUTFChars(jpath, path_chars);
    Core::Emulator emu;
    emu.Run(path, {});
    return 0;
}
