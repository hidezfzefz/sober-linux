/*
 * gl_egl_forward.c - GENERATED from docs/interfaces/libroblox-imports.txt.
 * EGL/GLES entry points are native x86-64 SysV register-preserving tail-call
 * trampolines into dlopen'd system libEGL/libGLESv2. This is a native graphics
 * driver call, NOT a translation layer. Owned by worker-3 (task #370).
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include "android_types.h"

static void *g_egl_lib;
static void *g_gles_lib;
static int   g_ready;

static void sober_gl_missing(const char *name)
{
    static int once;
    if (!once) {
        once = 1;
        fprintf(stderr, "[sober-android] native GL/EGL symbol unavailable: %s\n", name);
    }
}

void (*sober_fn_eglChooseConfig)(void) = NULL;
void (*sober_fn_eglCreateContext)(void) = NULL;
void (*sober_fn_eglCreatePbufferSurface)(void) = NULL;
void (*sober_fn_eglCreateWindowSurface)(void) = NULL;
void (*sober_fn_eglDestroyContext)(void) = NULL;
void (*sober_fn_eglDestroySurface)(void) = NULL;
void (*sober_fn_eglGetConfigAttrib)(void) = NULL;
void (*sober_fn_eglGetCurrentContext)(void) = NULL;
void (*sober_fn_eglGetDisplay)(void) = NULL;
void (*sober_fn_eglGetError)(void) = NULL;
void (*sober_fn_eglGetProcAddress)(void) = NULL;
void (*sober_fn_eglInitialize)(void) = NULL;
void (*sober_fn_eglMakeCurrent)(void) = NULL;
void (*sober_fn_eglQuerySurface)(void) = NULL;
void (*sober_fn_eglSwapBuffers)(void) = NULL;
void (*sober_fn_eglSwapInterval)(void) = NULL;
void (*sober_fn_eglTerminate)(void) = NULL;
void (*sober_fn_glActiveTexture)(void) = NULL;
void (*sober_fn_glAttachShader)(void) = NULL;
void (*sober_fn_glBindAttribLocation)(void) = NULL;
void (*sober_fn_glBindBuffer)(void) = NULL;
void (*sober_fn_glBindFramebuffer)(void) = NULL;
void (*sober_fn_glBindRenderbuffer)(void) = NULL;
void (*sober_fn_glBindTexture)(void) = NULL;
void (*sober_fn_glBlendFunc)(void) = NULL;
void (*sober_fn_glBlendFuncSeparate)(void) = NULL;
void (*sober_fn_glBufferData)(void) = NULL;
void (*sober_fn_glBufferSubData)(void) = NULL;
void (*sober_fn_glCheckFramebufferStatus)(void) = NULL;
void (*sober_fn_glClear)(void) = NULL;
void (*sober_fn_glClearColor)(void) = NULL;
void (*sober_fn_glClearDepthf)(void) = NULL;
void (*sober_fn_glClearStencil)(void) = NULL;
void (*sober_fn_glColorMask)(void) = NULL;
void (*sober_fn_glCompileShader)(void) = NULL;
void (*sober_fn_glCompressedTexImage2D)(void) = NULL;
void (*sober_fn_glCompressedTexSubImage2D)(void) = NULL;
void (*sober_fn_glCopyTexSubImage2D)(void) = NULL;
void (*sober_fn_glCreateProgram)(void) = NULL;
void (*sober_fn_glCreateShader)(void) = NULL;
void (*sober_fn_glCullFace)(void) = NULL;
void (*sober_fn_glDeleteBuffers)(void) = NULL;
void (*sober_fn_glDeleteFramebuffers)(void) = NULL;
void (*sober_fn_glDeleteProgram)(void) = NULL;
void (*sober_fn_glDeleteRenderbuffers)(void) = NULL;
void (*sober_fn_glDeleteShader)(void) = NULL;
void (*sober_fn_glDeleteTextures)(void) = NULL;
void (*sober_fn_glDepthFunc)(void) = NULL;
void (*sober_fn_glDepthMask)(void) = NULL;
void (*sober_fn_glDisable)(void) = NULL;
void (*sober_fn_glDisableVertexAttribArray)(void) = NULL;
void (*sober_fn_glDrawArrays)(void) = NULL;
void (*sober_fn_glDrawElements)(void) = NULL;
void (*sober_fn_glEnable)(void) = NULL;
void (*sober_fn_glEnableVertexAttribArray)(void) = NULL;
void (*sober_fn_glFramebufferRenderbuffer)(void) = NULL;
void (*sober_fn_glFramebufferTexture2D)(void) = NULL;
void (*sober_fn_glGenBuffers)(void) = NULL;
void (*sober_fn_glGenFramebuffers)(void) = NULL;
void (*sober_fn_glGenRenderbuffers)(void) = NULL;
void (*sober_fn_glGenTextures)(void) = NULL;
void (*sober_fn_glGenerateMipmap)(void) = NULL;
void (*sober_fn_glGetActiveUniform)(void) = NULL;
void (*sober_fn_glGetError)(void) = NULL;
void (*sober_fn_glGetIntegerv)(void) = NULL;
void (*sober_fn_glGetProgramInfoLog)(void) = NULL;
void (*sober_fn_glGetProgramiv)(void) = NULL;
void (*sober_fn_glGetShaderInfoLog)(void) = NULL;
void (*sober_fn_glGetShaderiv)(void) = NULL;
void (*sober_fn_glGetString)(void) = NULL;
void (*sober_fn_glGetUniformLocation)(void) = NULL;
void (*sober_fn_glLinkProgram)(void) = NULL;
void (*sober_fn_glPixelStorei)(void) = NULL;
void (*sober_fn_glPolygonOffset)(void) = NULL;
void (*sober_fn_glReadPixels)(void) = NULL;
void (*sober_fn_glReleaseShaderCompiler)(void) = NULL;
void (*sober_fn_glRenderbufferStorage)(void) = NULL;
void (*sober_fn_glScissor)(void) = NULL;
void (*sober_fn_glShaderSource)(void) = NULL;
void (*sober_fn_glStencilFunc)(void) = NULL;
void (*sober_fn_glStencilMask)(void) = NULL;
void (*sober_fn_glStencilOp)(void) = NULL;
void (*sober_fn_glTexImage2D)(void) = NULL;
void (*sober_fn_glTexParameterf)(void) = NULL;
void (*sober_fn_glTexParameterfv)(void) = NULL;
void (*sober_fn_glTexParameteri)(void) = NULL;
void (*sober_fn_glTexSubImage2D)(void) = NULL;
void (*sober_fn_glUniform1i)(void) = NULL;
void (*sober_fn_glUseProgram)(void) = NULL;
void (*sober_fn_glVertexAttribPointer)(void) = NULL;
void (*sober_fn_glViewport)(void) = NULL;

/* Deterministic fallbacks for symbols a given driver does not export. */
static void sober_missing_eglChooseConfig(void) { sober_gl_missing("eglChooseConfig"); }
static void sober_missing_eglCreateContext(void) { sober_gl_missing("eglCreateContext"); }
static void sober_missing_eglCreatePbufferSurface(void) { sober_gl_missing("eglCreatePbufferSurface"); }
static void sober_missing_eglCreateWindowSurface(void) { sober_gl_missing("eglCreateWindowSurface"); }
static void sober_missing_eglDestroyContext(void) { sober_gl_missing("eglDestroyContext"); }
static void sober_missing_eglDestroySurface(void) { sober_gl_missing("eglDestroySurface"); }
static void sober_missing_eglGetConfigAttrib(void) { sober_gl_missing("eglGetConfigAttrib"); }
static void sober_missing_eglGetCurrentContext(void) { sober_gl_missing("eglGetCurrentContext"); }
static void sober_missing_eglGetDisplay(void) { sober_gl_missing("eglGetDisplay"); }
static void sober_missing_eglGetError(void) { sober_gl_missing("eglGetError"); }
static void sober_missing_eglGetProcAddress(void) { sober_gl_missing("eglGetProcAddress"); }
static void sober_missing_eglInitialize(void) { sober_gl_missing("eglInitialize"); }
static void sober_missing_eglMakeCurrent(void) { sober_gl_missing("eglMakeCurrent"); }
static void sober_missing_eglQuerySurface(void) { sober_gl_missing("eglQuerySurface"); }
static void sober_missing_eglSwapBuffers(void) { sober_gl_missing("eglSwapBuffers"); }
static void sober_missing_eglSwapInterval(void) { sober_gl_missing("eglSwapInterval"); }
static void sober_missing_eglTerminate(void) { sober_gl_missing("eglTerminate"); }
static void sober_missing_glActiveTexture(void) { sober_gl_missing("glActiveTexture"); }
static void sober_missing_glAttachShader(void) { sober_gl_missing("glAttachShader"); }
static void sober_missing_glBindAttribLocation(void) { sober_gl_missing("glBindAttribLocation"); }
static void sober_missing_glBindBuffer(void) { sober_gl_missing("glBindBuffer"); }
static void sober_missing_glBindFramebuffer(void) { sober_gl_missing("glBindFramebuffer"); }
static void sober_missing_glBindRenderbuffer(void) { sober_gl_missing("glBindRenderbuffer"); }
static void sober_missing_glBindTexture(void) { sober_gl_missing("glBindTexture"); }
static void sober_missing_glBlendFunc(void) { sober_gl_missing("glBlendFunc"); }
static void sober_missing_glBlendFuncSeparate(void) { sober_gl_missing("glBlendFuncSeparate"); }
static void sober_missing_glBufferData(void) { sober_gl_missing("glBufferData"); }
static void sober_missing_glBufferSubData(void) { sober_gl_missing("glBufferSubData"); }
static void sober_missing_glCheckFramebufferStatus(void) { sober_gl_missing("glCheckFramebufferStatus"); }
static void sober_missing_glClear(void) { sober_gl_missing("glClear"); }
static void sober_missing_glClearColor(void) { sober_gl_missing("glClearColor"); }
static void sober_missing_glClearDepthf(void) { sober_gl_missing("glClearDepthf"); }
static void sober_missing_glClearStencil(void) { sober_gl_missing("glClearStencil"); }
static void sober_missing_glColorMask(void) { sober_gl_missing("glColorMask"); }
static void sober_missing_glCompileShader(void) { sober_gl_missing("glCompileShader"); }
static void sober_missing_glCompressedTexImage2D(void) { sober_gl_missing("glCompressedTexImage2D"); }
static void sober_missing_glCompressedTexSubImage2D(void) { sober_gl_missing("glCompressedTexSubImage2D"); }
static void sober_missing_glCopyTexSubImage2D(void) { sober_gl_missing("glCopyTexSubImage2D"); }
static void sober_missing_glCreateProgram(void) { sober_gl_missing("glCreateProgram"); }
static void sober_missing_glCreateShader(void) { sober_gl_missing("glCreateShader"); }
static void sober_missing_glCullFace(void) { sober_gl_missing("glCullFace"); }
static void sober_missing_glDeleteBuffers(void) { sober_gl_missing("glDeleteBuffers"); }
static void sober_missing_glDeleteFramebuffers(void) { sober_gl_missing("glDeleteFramebuffers"); }
static void sober_missing_glDeleteProgram(void) { sober_gl_missing("glDeleteProgram"); }
static void sober_missing_glDeleteRenderbuffers(void) { sober_gl_missing("glDeleteRenderbuffers"); }
static void sober_missing_glDeleteShader(void) { sober_gl_missing("glDeleteShader"); }
static void sober_missing_glDeleteTextures(void) { sober_gl_missing("glDeleteTextures"); }
static void sober_missing_glDepthFunc(void) { sober_gl_missing("glDepthFunc"); }
static void sober_missing_glDepthMask(void) { sober_gl_missing("glDepthMask"); }
static void sober_missing_glDisable(void) { sober_gl_missing("glDisable"); }
static void sober_missing_glDisableVertexAttribArray(void) { sober_gl_missing("glDisableVertexAttribArray"); }
static void sober_missing_glDrawArrays(void) { sober_gl_missing("glDrawArrays"); }
static void sober_missing_glDrawElements(void) { sober_gl_missing("glDrawElements"); }
static void sober_missing_glEnable(void) { sober_gl_missing("glEnable"); }
static void sober_missing_glEnableVertexAttribArray(void) { sober_gl_missing("glEnableVertexAttribArray"); }
static void sober_missing_glFramebufferRenderbuffer(void) { sober_gl_missing("glFramebufferRenderbuffer"); }
static void sober_missing_glFramebufferTexture2D(void) { sober_gl_missing("glFramebufferTexture2D"); }
static void sober_missing_glGenBuffers(void) { sober_gl_missing("glGenBuffers"); }
static void sober_missing_glGenFramebuffers(void) { sober_gl_missing("glGenFramebuffers"); }
static void sober_missing_glGenRenderbuffers(void) { sober_gl_missing("glGenRenderbuffers"); }
static void sober_missing_glGenTextures(void) { sober_gl_missing("glGenTextures"); }
static void sober_missing_glGenerateMipmap(void) { sober_gl_missing("glGenerateMipmap"); }
static void sober_missing_glGetActiveUniform(void) { sober_gl_missing("glGetActiveUniform"); }
static void sober_missing_glGetError(void) { sober_gl_missing("glGetError"); }
static void sober_missing_glGetIntegerv(void) { sober_gl_missing("glGetIntegerv"); }
static void sober_missing_glGetProgramInfoLog(void) { sober_gl_missing("glGetProgramInfoLog"); }
static void sober_missing_glGetProgramiv(void) { sober_gl_missing("glGetProgramiv"); }
static void sober_missing_glGetShaderInfoLog(void) { sober_gl_missing("glGetShaderInfoLog"); }
static void sober_missing_glGetShaderiv(void) { sober_gl_missing("glGetShaderiv"); }
static void sober_missing_glGetString(void) { sober_gl_missing("glGetString"); }
static void sober_missing_glGetUniformLocation(void) { sober_gl_missing("glGetUniformLocation"); }
static void sober_missing_glLinkProgram(void) { sober_gl_missing("glLinkProgram"); }
static void sober_missing_glPixelStorei(void) { sober_gl_missing("glPixelStorei"); }
static void sober_missing_glPolygonOffset(void) { sober_gl_missing("glPolygonOffset"); }
static void sober_missing_glReadPixels(void) { sober_gl_missing("glReadPixels"); }
static void sober_missing_glReleaseShaderCompiler(void) { sober_gl_missing("glReleaseShaderCompiler"); }
static void sober_missing_glRenderbufferStorage(void) { sober_gl_missing("glRenderbufferStorage"); }
static void sober_missing_glScissor(void) { sober_gl_missing("glScissor"); }
static void sober_missing_glShaderSource(void) { sober_gl_missing("glShaderSource"); }
static void sober_missing_glStencilFunc(void) { sober_gl_missing("glStencilFunc"); }
static void sober_missing_glStencilMask(void) { sober_gl_missing("glStencilMask"); }
static void sober_missing_glStencilOp(void) { sober_gl_missing("glStencilOp"); }
static void sober_missing_glTexImage2D(void) { sober_gl_missing("glTexImage2D"); }
static void sober_missing_glTexParameterf(void) { sober_gl_missing("glTexParameterf"); }
static void sober_missing_glTexParameterfv(void) { sober_gl_missing("glTexParameterfv"); }
static void sober_missing_glTexParameteri(void) { sober_gl_missing("glTexParameteri"); }
static void sober_missing_glTexSubImage2D(void) { sober_gl_missing("glTexSubImage2D"); }
static void sober_missing_glUniform1i(void) { sober_gl_missing("glUniform1i"); }
static void sober_missing_glUseProgram(void) { sober_gl_missing("glUseProgram"); }
static void sober_missing_glVertexAttribPointer(void) { sober_gl_missing("glVertexAttribPointer"); }
static void sober_missing_glViewport(void) { sober_gl_missing("glViewport"); }

#define TRAMP(name) \
    __asm__ volatile ( \
        ".globl " #name "\n\t" \
        ".type " #name ", @function\n\t" \
        #name ":\n\t" \
        "movq sober_fn_" #name "(%%rip), %%rax\n\t" \
        "testq %%rax, %%rax\n\t" \
        "jz 1f\n\t" \
        "jmp *%%rax\n\t" \
        "1: ret\n\t" \
        ".size " #name ", .-" #name "\n\t" \
        : : : "memory")

void sober_gl_egl_install_trampolines(void)
{
    TRAMP(eglChooseConfig);
    TRAMP(eglCreateContext);
    TRAMP(eglCreatePbufferSurface);
    TRAMP(eglCreateWindowSurface);
    TRAMP(eglDestroyContext);
    TRAMP(eglDestroySurface);
    TRAMP(eglGetConfigAttrib);
    TRAMP(eglGetCurrentContext);
    TRAMP(eglGetDisplay);
    TRAMP(eglGetError);
    TRAMP(eglGetProcAddress);
    TRAMP(eglInitialize);
    TRAMP(eglMakeCurrent);
    TRAMP(eglQuerySurface);
    TRAMP(eglSwapBuffers);
    TRAMP(eglSwapInterval);
    TRAMP(eglTerminate);
    TRAMP(glActiveTexture);
    TRAMP(glAttachShader);
    TRAMP(glBindAttribLocation);
    TRAMP(glBindBuffer);
    TRAMP(glBindFramebuffer);
    TRAMP(glBindRenderbuffer);
    TRAMP(glBindTexture);
    TRAMP(glBlendFunc);
    TRAMP(glBlendFuncSeparate);
    TRAMP(glBufferData);
    TRAMP(glBufferSubData);
    TRAMP(glCheckFramebufferStatus);
    TRAMP(glClear);
    TRAMP(glClearColor);
    TRAMP(glClearDepthf);
    TRAMP(glClearStencil);
    TRAMP(glColorMask);
    TRAMP(glCompileShader);
    TRAMP(glCompressedTexImage2D);
    TRAMP(glCompressedTexSubImage2D);
    TRAMP(glCopyTexSubImage2D);
    TRAMP(glCreateProgram);
    TRAMP(glCreateShader);
    TRAMP(glCullFace);
    TRAMP(glDeleteBuffers);
    TRAMP(glDeleteFramebuffers);
    TRAMP(glDeleteProgram);
    TRAMP(glDeleteRenderbuffers);
    TRAMP(glDeleteShader);
    TRAMP(glDeleteTextures);
    TRAMP(glDepthFunc);
    TRAMP(glDepthMask);
    TRAMP(glDisable);
    TRAMP(glDisableVertexAttribArray);
    TRAMP(glDrawArrays);
    TRAMP(glDrawElements);
    TRAMP(glEnable);
    TRAMP(glEnableVertexAttribArray);
    TRAMP(glFramebufferRenderbuffer);
    TRAMP(glFramebufferTexture2D);
    TRAMP(glGenBuffers);
    TRAMP(glGenFramebuffers);
    TRAMP(glGenRenderbuffers);
    TRAMP(glGenTextures);
    TRAMP(glGenerateMipmap);
    TRAMP(glGetActiveUniform);
    TRAMP(glGetError);
    TRAMP(glGetIntegerv);
    TRAMP(glGetProgramInfoLog);
    TRAMP(glGetProgramiv);
    TRAMP(glGetShaderInfoLog);
    TRAMP(glGetShaderiv);
    TRAMP(glGetString);
    TRAMP(glGetUniformLocation);
    TRAMP(glLinkProgram);
    TRAMP(glPixelStorei);
    TRAMP(glPolygonOffset);
    TRAMP(glReadPixels);
    TRAMP(glReleaseShaderCompiler);
    TRAMP(glRenderbufferStorage);
    TRAMP(glScissor);
    TRAMP(glShaderSource);
    TRAMP(glStencilFunc);
    TRAMP(glStencilMask);
    TRAMP(glStencilOp);
    TRAMP(glTexImage2D);
    TRAMP(glTexParameterf);
    TRAMP(glTexParameterfv);
    TRAMP(glTexParameteri);
    TRAMP(glTexSubImage2D);
    TRAMP(glUniform1i);
    TRAMP(glUseProgram);
    TRAMP(glVertexAttribPointer);
    TRAMP(glViewport);
}

static void *resolve_symbol(const char *name)
{
    void *p = g_gles_lib ? dlsym(g_gles_lib, name) : NULL;
    if (!p) p = g_egl_lib ? dlsym(g_egl_lib, name) : NULL;
    if (!p) p = dlsym(RTLD_DEFAULT, name);
    return p;
}

int sober_gl_egl_init(void)
{
    if (g_ready) return 1;
    g_egl_lib  = dlopen("libEGL.so.1", RTLD_LAZY | RTLD_LOCAL);
    g_gles_lib = dlopen("libGLESv2.so.2", RTLD_LAZY | RTLD_LOCAL);
    if (!g_gles_lib) g_gles_lib = dlopen("libGLESv2.so", RTLD_LAZY | RTLD_LOCAL);
    if (!g_egl_lib)  g_egl_lib  = dlopen("libEGL.so", RTLD_LAZY | RTLD_LOCAL);

    { void *p = resolve_symbol("eglChooseConfig"); sober_fn_eglChooseConfig = p ? (void (*)(void))p : (void (*)(void))sober_missing_eglChooseConfig; }
    { void *p = resolve_symbol("eglCreateContext"); sober_fn_eglCreateContext = p ? (void (*)(void))p : (void (*)(void))sober_missing_eglCreateContext; }
    { void *p = resolve_symbol("eglCreatePbufferSurface"); sober_fn_eglCreatePbufferSurface = p ? (void (*)(void))p : (void (*)(void))sober_missing_eglCreatePbufferSurface; }
    { void *p = resolve_symbol("eglCreateWindowSurface"); sober_fn_eglCreateWindowSurface = p ? (void (*)(void))p : (void (*)(void))sober_missing_eglCreateWindowSurface; }
    { void *p = resolve_symbol("eglDestroyContext"); sober_fn_eglDestroyContext = p ? (void (*)(void))p : (void (*)(void))sober_missing_eglDestroyContext; }
    { void *p = resolve_symbol("eglDestroySurface"); sober_fn_eglDestroySurface = p ? (void (*)(void))p : (void (*)(void))sober_missing_eglDestroySurface; }
    { void *p = resolve_symbol("eglGetConfigAttrib"); sober_fn_eglGetConfigAttrib = p ? (void (*)(void))p : (void (*)(void))sober_missing_eglGetConfigAttrib; }
    { void *p = resolve_symbol("eglGetCurrentContext"); sober_fn_eglGetCurrentContext = p ? (void (*)(void))p : (void (*)(void))sober_missing_eglGetCurrentContext; }
    { void *p = resolve_symbol("eglGetDisplay"); sober_fn_eglGetDisplay = p ? (void (*)(void))p : (void (*)(void))sober_missing_eglGetDisplay; }
    { void *p = resolve_symbol("eglGetError"); sober_fn_eglGetError = p ? (void (*)(void))p : (void (*)(void))sober_missing_eglGetError; }
    { void *p = resolve_symbol("eglGetProcAddress"); sober_fn_eglGetProcAddress = p ? (void (*)(void))p : (void (*)(void))sober_missing_eglGetProcAddress; }
    { void *p = resolve_symbol("eglInitialize"); sober_fn_eglInitialize = p ? (void (*)(void))p : (void (*)(void))sober_missing_eglInitialize; }
    { void *p = resolve_symbol("eglMakeCurrent"); sober_fn_eglMakeCurrent = p ? (void (*)(void))p : (void (*)(void))sober_missing_eglMakeCurrent; }
    { void *p = resolve_symbol("eglQuerySurface"); sober_fn_eglQuerySurface = p ? (void (*)(void))p : (void (*)(void))sober_missing_eglQuerySurface; }
    { void *p = resolve_symbol("eglSwapBuffers"); sober_fn_eglSwapBuffers = p ? (void (*)(void))p : (void (*)(void))sober_missing_eglSwapBuffers; }
    { void *p = resolve_symbol("eglSwapInterval"); sober_fn_eglSwapInterval = p ? (void (*)(void))p : (void (*)(void))sober_missing_eglSwapInterval; }
    { void *p = resolve_symbol("eglTerminate"); sober_fn_eglTerminate = p ? (void (*)(void))p : (void (*)(void))sober_missing_eglTerminate; }
    { void *p = resolve_symbol("glActiveTexture"); sober_fn_glActiveTexture = p ? (void (*)(void))p : (void (*)(void))sober_missing_glActiveTexture; }
    { void *p = resolve_symbol("glAttachShader"); sober_fn_glAttachShader = p ? (void (*)(void))p : (void (*)(void))sober_missing_glAttachShader; }
    { void *p = resolve_symbol("glBindAttribLocation"); sober_fn_glBindAttribLocation = p ? (void (*)(void))p : (void (*)(void))sober_missing_glBindAttribLocation; }
    { void *p = resolve_symbol("glBindBuffer"); sober_fn_glBindBuffer = p ? (void (*)(void))p : (void (*)(void))sober_missing_glBindBuffer; }
    { void *p = resolve_symbol("glBindFramebuffer"); sober_fn_glBindFramebuffer = p ? (void (*)(void))p : (void (*)(void))sober_missing_glBindFramebuffer; }
    { void *p = resolve_symbol("glBindRenderbuffer"); sober_fn_glBindRenderbuffer = p ? (void (*)(void))p : (void (*)(void))sober_missing_glBindRenderbuffer; }
    { void *p = resolve_symbol("glBindTexture"); sober_fn_glBindTexture = p ? (void (*)(void))p : (void (*)(void))sober_missing_glBindTexture; }
    { void *p = resolve_symbol("glBlendFunc"); sober_fn_glBlendFunc = p ? (void (*)(void))p : (void (*)(void))sober_missing_glBlendFunc; }
    { void *p = resolve_symbol("glBlendFuncSeparate"); sober_fn_glBlendFuncSeparate = p ? (void (*)(void))p : (void (*)(void))sober_missing_glBlendFuncSeparate; }
    { void *p = resolve_symbol("glBufferData"); sober_fn_glBufferData = p ? (void (*)(void))p : (void (*)(void))sober_missing_glBufferData; }
    { void *p = resolve_symbol("glBufferSubData"); sober_fn_glBufferSubData = p ? (void (*)(void))p : (void (*)(void))sober_missing_glBufferSubData; }
    { void *p = resolve_symbol("glCheckFramebufferStatus"); sober_fn_glCheckFramebufferStatus = p ? (void (*)(void))p : (void (*)(void))sober_missing_glCheckFramebufferStatus; }
    { void *p = resolve_symbol("glClear"); sober_fn_glClear = p ? (void (*)(void))p : (void (*)(void))sober_missing_glClear; }
    { void *p = resolve_symbol("glClearColor"); sober_fn_glClearColor = p ? (void (*)(void))p : (void (*)(void))sober_missing_glClearColor; }
    { void *p = resolve_symbol("glClearDepthf"); sober_fn_glClearDepthf = p ? (void (*)(void))p : (void (*)(void))sober_missing_glClearDepthf; }
    { void *p = resolve_symbol("glClearStencil"); sober_fn_glClearStencil = p ? (void (*)(void))p : (void (*)(void))sober_missing_glClearStencil; }
    { void *p = resolve_symbol("glColorMask"); sober_fn_glColorMask = p ? (void (*)(void))p : (void (*)(void))sober_missing_glColorMask; }
    { void *p = resolve_symbol("glCompileShader"); sober_fn_glCompileShader = p ? (void (*)(void))p : (void (*)(void))sober_missing_glCompileShader; }
    { void *p = resolve_symbol("glCompressedTexImage2D"); sober_fn_glCompressedTexImage2D = p ? (void (*)(void))p : (void (*)(void))sober_missing_glCompressedTexImage2D; }
    { void *p = resolve_symbol("glCompressedTexSubImage2D"); sober_fn_glCompressedTexSubImage2D = p ? (void (*)(void))p : (void (*)(void))sober_missing_glCompressedTexSubImage2D; }
    { void *p = resolve_symbol("glCopyTexSubImage2D"); sober_fn_glCopyTexSubImage2D = p ? (void (*)(void))p : (void (*)(void))sober_missing_glCopyTexSubImage2D; }
    { void *p = resolve_symbol("glCreateProgram"); sober_fn_glCreateProgram = p ? (void (*)(void))p : (void (*)(void))sober_missing_glCreateProgram; }
    { void *p = resolve_symbol("glCreateShader"); sober_fn_glCreateShader = p ? (void (*)(void))p : (void (*)(void))sober_missing_glCreateShader; }
    { void *p = resolve_symbol("glCullFace"); sober_fn_glCullFace = p ? (void (*)(void))p : (void (*)(void))sober_missing_glCullFace; }
    { void *p = resolve_symbol("glDeleteBuffers"); sober_fn_glDeleteBuffers = p ? (void (*)(void))p : (void (*)(void))sober_missing_glDeleteBuffers; }
    { void *p = resolve_symbol("glDeleteFramebuffers"); sober_fn_glDeleteFramebuffers = p ? (void (*)(void))p : (void (*)(void))sober_missing_glDeleteFramebuffers; }
    { void *p = resolve_symbol("glDeleteProgram"); sober_fn_glDeleteProgram = p ? (void (*)(void))p : (void (*)(void))sober_missing_glDeleteProgram; }
    { void *p = resolve_symbol("glDeleteRenderbuffers"); sober_fn_glDeleteRenderbuffers = p ? (void (*)(void))p : (void (*)(void))sober_missing_glDeleteRenderbuffers; }
    { void *p = resolve_symbol("glDeleteShader"); sober_fn_glDeleteShader = p ? (void (*)(void))p : (void (*)(void))sober_missing_glDeleteShader; }
    { void *p = resolve_symbol("glDeleteTextures"); sober_fn_glDeleteTextures = p ? (void (*)(void))p : (void (*)(void))sober_missing_glDeleteTextures; }
    { void *p = resolve_symbol("glDepthFunc"); sober_fn_glDepthFunc = p ? (void (*)(void))p : (void (*)(void))sober_missing_glDepthFunc; }
    { void *p = resolve_symbol("glDepthMask"); sober_fn_glDepthMask = p ? (void (*)(void))p : (void (*)(void))sober_missing_glDepthMask; }
    { void *p = resolve_symbol("glDisable"); sober_fn_glDisable = p ? (void (*)(void))p : (void (*)(void))sober_missing_glDisable; }
    { void *p = resolve_symbol("glDisableVertexAttribArray"); sober_fn_glDisableVertexAttribArray = p ? (void (*)(void))p : (void (*)(void))sober_missing_glDisableVertexAttribArray; }
    { void *p = resolve_symbol("glDrawArrays"); sober_fn_glDrawArrays = p ? (void (*)(void))p : (void (*)(void))sober_missing_glDrawArrays; }
    { void *p = resolve_symbol("glDrawElements"); sober_fn_glDrawElements = p ? (void (*)(void))p : (void (*)(void))sober_missing_glDrawElements; }
    { void *p = resolve_symbol("glEnable"); sober_fn_glEnable = p ? (void (*)(void))p : (void (*)(void))sober_missing_glEnable; }
    { void *p = resolve_symbol("glEnableVertexAttribArray"); sober_fn_glEnableVertexAttribArray = p ? (void (*)(void))p : (void (*)(void))sober_missing_glEnableVertexAttribArray; }
    { void *p = resolve_symbol("glFramebufferRenderbuffer"); sober_fn_glFramebufferRenderbuffer = p ? (void (*)(void))p : (void (*)(void))sober_missing_glFramebufferRenderbuffer; }
    { void *p = resolve_symbol("glFramebufferTexture2D"); sober_fn_glFramebufferTexture2D = p ? (void (*)(void))p : (void (*)(void))sober_missing_glFramebufferTexture2D; }
    { void *p = resolve_symbol("glGenBuffers"); sober_fn_glGenBuffers = p ? (void (*)(void))p : (void (*)(void))sober_missing_glGenBuffers; }
    { void *p = resolve_symbol("glGenFramebuffers"); sober_fn_glGenFramebuffers = p ? (void (*)(void))p : (void (*)(void))sober_missing_glGenFramebuffers; }
    { void *p = resolve_symbol("glGenRenderbuffers"); sober_fn_glGenRenderbuffers = p ? (void (*)(void))p : (void (*)(void))sober_missing_glGenRenderbuffers; }
    { void *p = resolve_symbol("glGenTextures"); sober_fn_glGenTextures = p ? (void (*)(void))p : (void (*)(void))sober_missing_glGenTextures; }
    { void *p = resolve_symbol("glGenerateMipmap"); sober_fn_glGenerateMipmap = p ? (void (*)(void))p : (void (*)(void))sober_missing_glGenerateMipmap; }
    { void *p = resolve_symbol("glGetActiveUniform"); sober_fn_glGetActiveUniform = p ? (void (*)(void))p : (void (*)(void))sober_missing_glGetActiveUniform; }
    { void *p = resolve_symbol("glGetError"); sober_fn_glGetError = p ? (void (*)(void))p : (void (*)(void))sober_missing_glGetError; }
    { void *p = resolve_symbol("glGetIntegerv"); sober_fn_glGetIntegerv = p ? (void (*)(void))p : (void (*)(void))sober_missing_glGetIntegerv; }
    { void *p = resolve_symbol("glGetProgramInfoLog"); sober_fn_glGetProgramInfoLog = p ? (void (*)(void))p : (void (*)(void))sober_missing_glGetProgramInfoLog; }
    { void *p = resolve_symbol("glGetProgramiv"); sober_fn_glGetProgramiv = p ? (void (*)(void))p : (void (*)(void))sober_missing_glGetProgramiv; }
    { void *p = resolve_symbol("glGetShaderInfoLog"); sober_fn_glGetShaderInfoLog = p ? (void (*)(void))p : (void (*)(void))sober_missing_glGetShaderInfoLog; }
    { void *p = resolve_symbol("glGetShaderiv"); sober_fn_glGetShaderiv = p ? (void (*)(void))p : (void (*)(void))sober_missing_glGetShaderiv; }
    { void *p = resolve_symbol("glGetString"); sober_fn_glGetString = p ? (void (*)(void))p : (void (*)(void))sober_missing_glGetString; }
    { void *p = resolve_symbol("glGetUniformLocation"); sober_fn_glGetUniformLocation = p ? (void (*)(void))p : (void (*)(void))sober_missing_glGetUniformLocation; }
    { void *p = resolve_symbol("glLinkProgram"); sober_fn_glLinkProgram = p ? (void (*)(void))p : (void (*)(void))sober_missing_glLinkProgram; }
    { void *p = resolve_symbol("glPixelStorei"); sober_fn_glPixelStorei = p ? (void (*)(void))p : (void (*)(void))sober_missing_glPixelStorei; }
    { void *p = resolve_symbol("glPolygonOffset"); sober_fn_glPolygonOffset = p ? (void (*)(void))p : (void (*)(void))sober_missing_glPolygonOffset; }
    { void *p = resolve_symbol("glReadPixels"); sober_fn_glReadPixels = p ? (void (*)(void))p : (void (*)(void))sober_missing_glReadPixels; }
    { void *p = resolve_symbol("glReleaseShaderCompiler"); sober_fn_glReleaseShaderCompiler = p ? (void (*)(void))p : (void (*)(void))sober_missing_glReleaseShaderCompiler; }
    { void *p = resolve_symbol("glRenderbufferStorage"); sober_fn_glRenderbufferStorage = p ? (void (*)(void))p : (void (*)(void))sober_missing_glRenderbufferStorage; }
    { void *p = resolve_symbol("glScissor"); sober_fn_glScissor = p ? (void (*)(void))p : (void (*)(void))sober_missing_glScissor; }
    { void *p = resolve_symbol("glShaderSource"); sober_fn_glShaderSource = p ? (void (*)(void))p : (void (*)(void))sober_missing_glShaderSource; }
    { void *p = resolve_symbol("glStencilFunc"); sober_fn_glStencilFunc = p ? (void (*)(void))p : (void (*)(void))sober_missing_glStencilFunc; }
    { void *p = resolve_symbol("glStencilMask"); sober_fn_glStencilMask = p ? (void (*)(void))p : (void (*)(void))sober_missing_glStencilMask; }
    { void *p = resolve_symbol("glStencilOp"); sober_fn_glStencilOp = p ? (void (*)(void))p : (void (*)(void))sober_missing_glStencilOp; }
    { void *p = resolve_symbol("glTexImage2D"); sober_fn_glTexImage2D = p ? (void (*)(void))p : (void (*)(void))sober_missing_glTexImage2D; }
    { void *p = resolve_symbol("glTexParameterf"); sober_fn_glTexParameterf = p ? (void (*)(void))p : (void (*)(void))sober_missing_glTexParameterf; }
    { void *p = resolve_symbol("glTexParameterfv"); sober_fn_glTexParameterfv = p ? (void (*)(void))p : (void (*)(void))sober_missing_glTexParameterfv; }
    { void *p = resolve_symbol("glTexParameteri"); sober_fn_glTexParameteri = p ? (void (*)(void))p : (void (*)(void))sober_missing_glTexParameteri; }
    { void *p = resolve_symbol("glTexSubImage2D"); sober_fn_glTexSubImage2D = p ? (void (*)(void))p : (void (*)(void))sober_missing_glTexSubImage2D; }
    { void *p = resolve_symbol("glUniform1i"); sober_fn_glUniform1i = p ? (void (*)(void))p : (void (*)(void))sober_missing_glUniform1i; }
    { void *p = resolve_symbol("glUseProgram"); sober_fn_glUseProgram = p ? (void (*)(void))p : (void (*)(void))sober_missing_glUseProgram; }
    { void *p = resolve_symbol("glVertexAttribPointer"); sober_fn_glVertexAttribPointer = p ? (void (*)(void))p : (void (*)(void))sober_missing_glVertexAttribPointer; }
    { void *p = resolve_symbol("glViewport"); sober_fn_glViewport = p ? (void (*)(void))p : (void (*)(void))sober_missing_glViewport; }
    g_ready = 1;
    return (g_egl_lib && g_gles_lib) ? 1 : 0;
}

void sober_gl_egl_shutdown(void)
{
    if (g_gles_lib) { dlclose(g_gles_lib); g_gles_lib = NULL; }
    if (g_egl_lib)  { dlclose(g_egl_lib);  g_egl_lib  = NULL; }
    g_ready = 0;
}

const char *sober_gl_egl_backend(void)
{
    if (g_egl_lib && g_gles_lib) return "native libEGL + libGLESv2 (dlopen)";
    if (g_egl_lib) return "native libEGL (dlopen)";
    return "GL/EGL unavailable (headless)";
}

unsigned sober_gl_egl_symbol_count(void) { return 91u; }
