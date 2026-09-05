// Temporary JVMTI instrumentation for an owner's debuggable povo process.
// No certificate validation changes persist after the app process exits.
#include <jni.h>
#include <jvmti.h>
#include <android/log.h>
#include <cstring>

static void JNICALL onBreakpoint(jvmtiEnv* ti, JNIEnv*, jthread thread,
                                 jmethodID, jlocation) {
  const auto error = ti->ForceEarlyReturnVoid(thread);
  if (error != JVMTI_ERROR_NONE)
    __android_log_print(ANDROID_LOG_ERROR, "PovoCapture", "return error=%d", error);
}

extern "C" JNIEXPORT jint JNICALL Agent_OnAttach(JavaVM* vm, char*, void*) {
  jvmtiEnv* ti = nullptr;
  if (vm->GetEnv(reinterpret_cast<void**>(&ti), JVMTI_VERSION_1_2) != JNI_OK)
    return JNI_ERR;
  jvmtiCapabilities caps{};
  caps.can_generate_breakpoint_events = 1;
  caps.can_force_early_return = 1;
  auto error = ti->AddCapabilities(&caps);
  if (error != JVMTI_ERROR_NONE) {
    __android_log_print(ANDROID_LOG_ERROR, "PovoCapture", "capability error=%d", error);
    return JNI_ERR;
  }
  jvmtiEventCallbacks callbacks{};
  callbacks.Breakpoint = onBreakpoint;
  if (ti->SetEventCallbacks(&callbacks, sizeof(callbacks)) != JVMTI_ERROR_NONE)
    return JNI_ERR;
  jint count = 0;
  jclass* classes = nullptr;
  if (ti->GetLoadedClasses(&count, &classes) != JVMTI_ERROR_NONE) return JNI_ERR;
  int installed = 0;
  for (jint i = 0; i < count; ++i) {
    char* signature = nullptr;
    ti->GetClassSignature(classes[i], &signature, nullptr);
    if (signature && std::strcmp(signature, "Lokhttp3/CertificatePinner;") == 0) {
      jint methodCount = 0;
      jmethodID* methods = nullptr;
      if (ti->GetClassMethods(classes[i], &methodCount, &methods) == JVMTI_ERROR_NONE) {
        for (jint m = 0; m < methodCount; ++m) {
          char* name = nullptr;
          ti->GetMethodName(methods[m], &name, nullptr, nullptr);
          if (name && std::strcmp(name, "check$okhttp") == 0) {
            error = ti->SetBreakpoint(methods[m], 0);
            if (error == JVMTI_ERROR_NONE) ++installed;
          }
          ti->Deallocate(reinterpret_cast<unsigned char*>(name));
        }
        ti->Deallocate(reinterpret_cast<unsigned char*>(methods));
      }
    }
    ti->Deallocate(reinterpret_cast<unsigned char*>(signature));
  }
  ti->Deallocate(reinterpret_cast<unsigned char*>(classes));
  if (installed != 1) {
    __android_log_print(ANDROID_LOG_ERROR, "PovoCapture", "breakpoint count=%d", installed);
    return JNI_ERR;
  }
  error = ti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_BREAKPOINT, nullptr);
  __android_log_print(ANDROID_LOG_INFO, "PovoCapture", "attached error=%d", error);
  return error == JVMTI_ERROR_NONE ? JNI_OK : JNI_ERR;
}
