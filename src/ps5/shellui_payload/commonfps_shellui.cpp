/*
 * Common FPS for PS5
 * Copyright (C) 2026 porhe911
 * Modifications Copyright (C) 2026 khalifa007 (SimpleFPS)
 * Modifications Copyright (C) 2026 erickdavestech
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "commonfps_shellui.hpp"
#include "common_fps/shellui_hook_protocol.hpp"
#include "pad_assets_640.c"

extern "C" {
int scePadOpen(int user_id, int type, int index, const void* param);
int scePadGetHandle(int user_id, int type, int index);
int scePadReadState(int handle, void* data);
int sceUserServiceGetForegroundUser(int* user_id);
}

#include <arpa/inet.h>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace common_fps::ps5::shellui {

namespace {

static_assert(SOL_SOCKET == 0xffff);
static_assert(SO_RCVTIMEO == 0x1006);
static_assert(sizeof(timeval) == 16);

struct MonoDomain;
struct MonoThread;
struct MonoAssembly;
struct MonoImage;
struct MonoClass;
struct MonoMethod;
struct MonoProperty;
struct MonoObject;
struct MonoString;

using mono_get_root_domain_t = MonoDomain* (*)();
using mono_domain_get_t = MonoDomain* (*)();
using mono_thread_attach_t = MonoThread* (*)(MonoDomain*);
using mono_domain_assembly_open_t = MonoAssembly* (*)(MonoDomain*, const char*);
using mono_assembly_get_image_t = MonoImage* (*)(MonoAssembly*);
using mono_class_from_name_t = MonoClass* (*)(MonoImage*, const char*, const char*);
using mono_class_get_method_from_name_t = MonoMethod* (*)(MonoClass*, const char*, int);
using mono_class_get_property_from_name_t = MonoProperty* (*)(MonoClass*, const char*);
using mono_property_get_get_method_t = MonoMethod* (*)(MonoProperty*);
using mono_property_get_set_method_t = MonoMethod* (*)(MonoProperty*);
using mono_runtime_invoke_t = MonoObject* (*)(MonoMethod*, void*, void**, MonoObject**);
using mono_string_new_t = MonoString* (*)(MonoDomain*, const char*);
using mono_object_new_t = MonoObject* (*)(MonoDomain*, MonoClass*);
using mono_runtime_object_init_t = void (*)(MonoObject*);
using mono_object_unbox_t = void* (*)(MonoObject*);
using mono_compile_method_t = void* (*)(MonoMethod*);
using mono_gchandle_new_t = std::uint32_t (*)(MonoObject*, int);
using mono_gchandle_get_target_t = MonoObject* (*)(std::uint32_t);
using mono_gchandle_free_t = void (*)(std::uint32_t);

extern "C" {
MonoDomain* mono_get_root_domain();
MonoDomain* mono_domain_get();
MonoThread* mono_thread_attach(MonoDomain*);
MonoAssembly* mono_domain_assembly_open(MonoDomain*, const char*);
MonoImage* mono_assembly_get_image(MonoAssembly*);
MonoClass* mono_class_from_name(MonoImage*, const char*, const char*);
MonoMethod* mono_class_get_method_from_name(MonoClass*, const char*, int);
MonoProperty* mono_class_get_property_from_name(MonoClass*, const char*);
MonoMethod* mono_property_get_get_method(MonoProperty*);
MonoMethod* mono_property_get_set_method(MonoProperty*);
MonoObject* mono_runtime_invoke(MonoMethod*, void*, void**, MonoObject**);
MonoString* mono_string_new(MonoDomain*, const char*);
MonoObject* mono_object_new(MonoDomain*, MonoClass*);
void mono_runtime_object_init(MonoObject*);
void* mono_object_unbox(MonoObject*);
void* mono_compile_method(MonoMethod*);
std::uint32_t mono_gchandle_new(MonoObject*, int);
MonoObject* mono_gchandle_get_target(std::uint32_t);
void mono_gchandle_free(std::uint32_t);
}

mono_get_root_domain_t mono_get_root_domain_ = mono_get_root_domain;
mono_domain_get_t mono_domain_get_ = mono_domain_get;
mono_thread_attach_t mono_thread_attach_ = mono_thread_attach;
mono_domain_assembly_open_t mono_domain_assembly_open_ =
    mono_domain_assembly_open;
mono_assembly_get_image_t mono_assembly_get_image_ = mono_assembly_get_image;
mono_class_from_name_t mono_class_from_name_ = mono_class_from_name;
mono_class_get_method_from_name_t mono_class_get_method_from_name_ =
    mono_class_get_method_from_name;
mono_class_get_property_from_name_t mono_class_get_property_from_name_ =
    mono_class_get_property_from_name;
mono_property_get_get_method_t mono_property_get_get_method_ =
    mono_property_get_get_method;
mono_property_get_set_method_t mono_property_get_set_method_ =
    mono_property_get_set_method;
mono_runtime_invoke_t mono_runtime_invoke_ = mono_runtime_invoke;
mono_string_new_t mono_string_new_ = mono_string_new;
mono_object_new_t mono_object_new_ = mono_object_new;
mono_runtime_object_init_t mono_runtime_object_init_ =
    mono_runtime_object_init;
mono_object_unbox_t mono_object_unbox_ = mono_object_unbox;
mono_compile_method_t mono_compile_method_ = mono_compile_method;
mono_gchandle_new_t mono_gchandle_new_ = mono_gchandle_new;
mono_gchandle_get_target_t mono_gchandle_get_target_ =
    mono_gchandle_get_target;
mono_gchandle_free_t mono_gchandle_free_ = mono_gchandle_free;

MonoDomain* g_domain{};
MonoImage* g_pui_image{};
std::uint32_t g_game_scene_handle{};
std::uint32_t g_label_handle{};
std::uint32_t g_value_handle{};

std::uint32_t g_h_base{};
std::uint32_t g_h_btn[16]{};
std::uint32_t g_h_stick[2]{};
std::uint32_t g_h_stick_l3[2]{};
std::uint32_t g_h_trig[2]{};
std::uint32_t g_h_touch{};
int g_pad_handle = -1;
int g_pad_init_state = 0;
MonoMethod* g_find_scene_method{};
bool g_background_render_mode = false;

bool g_sprites_built = false;
bool g_scene_changed = false;
std::uint32_t g_last_buttons = 0xffffffffu;
std::uint16_t g_last_axis[2] = {0xffff, 0xffff};
bool g_last_ring[2] = {false, false};
float g_last_trig_frac[2] = {-1.0f, -1.0f};
bool g_last_touch = false;

using application_update_t = void (*)(MonoObject*);
application_update_t g_application_update_original{};
bool g_hook_install_confirmed = false;
bool g_hook_attempted = false;

std::atomic_bool g_runtime_ready{false};
std::atomic_bool g_have_packet{false};
std::atomic<std::uint64_t> g_sequence{0};
std::atomic<std::uint64_t> g_applied_sequence{0};

int g_receiver_fd = -1;
WirePacket g_packet{};
pthread_mutex_t g_packet_lock = PTHREAD_MUTEX_INITIALIZER;

constexpr const char* kLabelId = "id_commonfps_label";
constexpr const char* kValueId = "id_commonfps_value";
[[maybe_unused]] constexpr const char* kPadImgId = "id_padoverlay_base";

float kPadOvX = 40.0f;
float kPadOvY = 110.0f;
constexpr float kStickTravel = 10.0f;

constexpr float kAS = 0.5f;
constexpr float kPadOvW = 640.0f * kAS, kPadOvH = 480.0f * kAS;
constexpr const char* kPuiDll =
    "/system_ex/common_ex/lib/Sce.PlayStation.PUI.dll";
constexpr const char* kAppSystemDll =
    "/system_ex/common_ex/lib/Sce.Vsh.ShellUI.AppSystem.dll";
constexpr const char* kCoreDll =
    "/system_ex/common_ex/lib/Sce.PlayStation.Core.dll";

extern "C" __attribute__((naked, noinline, used, aligned(16)))
void commonfps_update_trampoline() {
    __asm__ volatile(
        ".rept 128\n"
        "nop\n"
        ".endr\n"
        "ret\n");
}

void application_update_hook(MonoObject* instance);

constexpr std::size_t kAbsoluteJumpSize = 14;
constexpr std::size_t kAtomicPatchSize = 16;
constexpr std::size_t kTrampolineCapacity = 128;

void encode_absolute_jump(
    std::uint8_t* output,
    const void* destination) noexcept {

    static constexpr std::uint8_t kPrefix[6] = {
        0xff, 0x25, 0x00, 0x00, 0x00, 0x00,
    };
    std::memcpy(output, kPrefix, sizeof(kPrefix));
    const std::uint64_t target =
        reinterpret_cast<std::uint64_t>(destination);
    std::memcpy(output + sizeof(kPrefix), &target, sizeof(target));
}

bool prepare_trampoline(
    const std::uint8_t* original,
    std::size_t original_size,
    const void* continuation) noexcept {

    if (!original || original_size == 0 ||
        original_size + (continuation ? kAbsoluteJumpSize : 0) >
            kTrampolineCapacity) {
        return false;
    }

    auto* trampoline = reinterpret_cast<std::uint8_t*>(
        &commonfps_update_trampoline);
    std::memcpy(trampoline, original, original_size);
    std::size_t written = original_size;
    if (continuation) {
        encode_absolute_jump(trampoline + written, continuation);
        written += kAbsoluteJumpSize;
    }

    __builtin___clear_cache(
        reinterpret_cast<char*>(trampoline),
        reinterpret_cast<char*>(trampoline + written));
    return true;
}

std::size_t decode_relocatable_instruction(
    const std::uint8_t* code,
    std::size_t available) noexcept {

    if (!code || available == 0)
        return 0;

    std::size_t cursor = 0;
    bool rex_w = false;
    bool operand16 = false;

    while (cursor < available) {
        const std::uint8_t byte = code[cursor];
        if (byte == 0x66) {
            operand16 = true;
            ++cursor;
            continue;
        }
        if (byte == 0x67) {

            return 0;
        }
        if (byte == 0xf0 || byte == 0xf2 || byte == 0xf3 ||
            byte == 0x2e || byte == 0x36 || byte == 0x3e ||
            byte == 0x26 || byte == 0x64 || byte == 0x65) {
            ++cursor;
            continue;
        }
        if (byte >= 0x40 && byte <= 0x4f) {
            rex_w = (byte & 0x08U) != 0;
            ++cursor;
            continue;
        }
        break;
    }

    if (cursor >= available)
        return 0;

    const std::uint8_t opcode = code[cursor++];

    if ((opcode >= 0x50 && opcode <= 0x5f) || opcode == 0x90)
        return cursor;

    if (opcode == 0x68) {
        const std::size_t immediate = operand16 ? 2U : 4U;
        return cursor + immediate <= available
            ? cursor + immediate
            : 0;
    }
    if (opcode == 0x6a)
        return cursor + 1U <= available ? cursor + 1U : 0;

    if (opcode >= 0xb8 && opcode <= 0xbf) {
        const std::size_t immediate = rex_w ? 8U : (operand16 ? 2U : 4U);
        return cursor + immediate <= available
            ? cursor + immediate
            : 0;
    }

    if (opcode == 0xe8 || opcode == 0xe9 || opcode == 0xeb ||
        (opcode >= 0x70 && opcode <= 0x7f) ||
        (opcode >= 0xe0 && opcode <= 0xe3)) {
        return 0;
    }

    std::size_t immediate = 0;
    bool has_modrm = false;

    switch (opcode) {
    case 0x01: case 0x03: case 0x09: case 0x0b:
    case 0x21: case 0x23: case 0x29: case 0x2b:
    case 0x31: case 0x33: case 0x39: case 0x3b:
    case 0x63: case 0x85: case 0x87: case 0x89:
    case 0x8b: case 0x8d:
        has_modrm = true;
        break;
    case 0x80: case 0x82: case 0x83: case 0xc6:
        has_modrm = true;
        immediate = 1;
        break;
    case 0x81: case 0xc7:
        has_modrm = true;
        immediate = operand16 ? 2U : 4U;
        break;
    case 0x0f: {
        if (cursor >= available)
            return 0;
        const std::uint8_t second = code[cursor++];
        if (second >= 0x80 && second <= 0x8f)
            return 0;
        if (second == 0x1e || second == 0x1f ||
            second == 0xb6 || second == 0xb7 ||
            second == 0xbe || second == 0xbf) {
            has_modrm = true;
            break;
        }
        return 0;
    }
    default:
        return 0;
    }

    if (!has_modrm || cursor >= available)
        return 0;

    const std::uint8_t modrm = code[cursor++];
    const std::uint8_t mod = static_cast<std::uint8_t>(modrm >> 6);
    const std::uint8_t rm = static_cast<std::uint8_t>(modrm & 7U);

    if (mod != 3 && rm == 4) {
        if (cursor >= available)
            return 0;
        const std::uint8_t sib = code[cursor++];
        const std::uint8_t base = static_cast<std::uint8_t>(sib & 7U);
        if (mod == 0 && base == 5) {
            if (cursor + 4U > available)
                return 0;
            cursor += 4U;
        }
    } else if (mod == 0 && rm == 5) {

        return 0;
    }

    if (mod == 1) {
        if (cursor + 1U > available)
            return 0;
        cursor += 1U;
    } else if (mod == 2) {
        if (cursor + 4U > available)
            return 0;
        cursor += 4U;
    }

    if (cursor + immediate > available)
        return 0;
    return cursor + immediate;
}

std::size_t native_patch_length(
    const std::uint8_t* code) noexcept {

    std::size_t length = 0;
    while (length < kAbsoluteJumpSize) {
        const std::size_t decoded = decode_relocatable_instruction(
            code + length,
            kAtomicPatchSize - length);
        if (decoded == 0)
            return 0;
        length += decoded;
    }

    return length <= kAtomicPatchSize ? length : 0;
}

template <typename T>
bool write_exact_file(
    const char* path,
    const T& value) noexcept {

    FILE* fp = std::fopen(path, "wb");
    if (!fp)
        return false;

    const bool complete =
        std::fwrite(&value, 1, sizeof(value), fp) == sizeof(value) &&
        std::fflush(fp) == 0;
    const bool closed = std::fclose(fp) == 0;
    return complete && closed;
}

template <typename T>
bool read_exact_file(const char* path, T& value) noexcept {
    FILE* fp = std::fopen(path, "rb");
    if (!fp)
        return false;

    const std::size_t count = std::fread(&value, 1, sizeof(value), fp);
    std::fclose(fp);
    return count == sizeof(value);
}

bool publish_hook_request(
    std::uint8_t* method,
    const std::array<std::uint8_t, kAtomicPatchSize>& expected,
    const std::array<std::uint8_t, kAtomicPatchSize>& desired,
    std::size_t displaced_size,
    application_update_t original_call,
    std::uint64_t& nonce) noexcept {

    ShellUiHookRequest request{};
    request.pid = getpid();
    request.method_address =
        reinterpret_cast<std::uint64_t>(method);
    request.hook_address =
        reinterpret_cast<std::uint64_t>(&application_update_hook);
    request.original_call_address =
        reinterpret_cast<std::uint64_t>(original_call);
    request.displaced_size = static_cast<std::uint32_t>(displaced_size);
    std::memcpy(request.expected, expected.data(), expected.size());
    std::memcpy(request.desired, desired.data(), desired.size());
    request.nonce = request.method_address ^ request.hook_address ^
        request.original_call_address ^
        (static_cast<std::uint64_t>(request.pid) << 32U) ^
        0x8d4f23a76c19e502ULL;
    if (request.nonce == 0)
        request.nonce = 1;
    request.checksum = shellui_hook_request_checksum(request);
    nonce = request.nonce;

    return write_exact_file(
        kShellUiHookRequestPath,
        request);
}

bool wait_for_native_hook_ack(
    pid_t pid,
    std::uint64_t nonce,
    ShellUiHookAck& ack) noexcept;

void log_line(const char* fmt, ...);
MonoImage* open_image(const char* path);

bool probe_hook_bytes(
    std::uint8_t* method,
    std::array<std::uint8_t, kAtomicPatchSize>& expected) noexcept {

    const std::array<std::uint8_t, kAtomicPatchSize> empty{};
    std::uint64_t nonce = 0;
    record_stage("hook_probe_request");
    if (!publish_hook_request(
            method,
            empty,
            empty,
            0,
            nullptr,
            nonce)) {
        log_line(
            "Application.Update probe publish failed method=%p",
            static_cast<void*>(method));
        return false;
    }

    ShellUiHookAck ack{};
    record_stage("hook_probe_ack_wait");
    if (!wait_for_native_hook_ack(getpid(), nonce, ack)) {
        log_line(
            "Application.Update probe timeout method=%p nonce=0x%llx",
            static_cast<void*>(method),
            static_cast<unsigned long long>(nonce));
        return false;
    }

    if (ack.phase != static_cast<std::uint8_t>(ShellUiHookAckPhase::Probe) ||
        ack.status != static_cast<std::int32_t>(ShellUiHookStatus::Success) ||
        ack.verified == 0 || ack.detached == 0 || ack.auth_restored == 0) {
        log_line(
            "Application.Update probe failed method=%p status=%d "
            "read_rc=%d verified=%u detached=%u auth_restored=%u phase=%u",
            static_cast<void*>(method),
            ack.status,
            ack.read_rc,
            static_cast<unsigned>(ack.verified),
            static_cast<unsigned>(ack.detached),
            static_cast<unsigned>(ack.auth_restored),
            static_cast<unsigned>(ack.phase));
        return false;
    }

    std::memcpy(expected.data(), ack.observed, expected.size());
    return true;
}

bool wait_for_native_hook_ack(
    pid_t pid,
    std::uint64_t nonce,
    ShellUiHookAck& ack) noexcept {

    for (int attempt = 0; attempt < 1000; ++attempt) {
        ShellUiHookAck candidate{};
        if (read_exact_file(kShellUiHookAckPath, candidate) &&
            candidate.magic == kShellUiHookAckMagic &&
            candidate.version == kShellUiHookProtocolVersion &&
            candidate.pid == pid &&
            candidate.nonce == nonce &&
            candidate.checksum == shellui_hook_ack_checksum(candidate)) {
            ack = candidate;
            return true;
        }
        usleep(20000);
    }
    return false;
}

enum class MainThreadGuardResult {
    Installed,
    Unsupported,
    Failed,
};

bool publish_main_thread_guard_request(
    std::uint8_t* method,
    const std::array<std::uint8_t, kAtomicPatchSize>& expected,
    std::uint64_t& nonce) noexcept {

    ShellUiHookRequest request{};
    request.pid = getpid();
    request.method_address = reinterpret_cast<std::uint64_t>(method);
    request.hook_address = request.method_address;
    request.original_call_address = 0;
    request.displaced_size = 1;
    std::memcpy(request.expected, expected.data(), expected.size());
    std::memcpy(request.desired, expected.data(), expected.size());
    request.desired[0] = 0xc3;
    request.nonce = request.method_address ^
        (static_cast<std::uint64_t>(request.pid) << 32U) ^
        0x6d9bc2f1a54837e0ULL;
    if (request.nonce == 0)
        request.nonce = 1;
    request.checksum = shellui_hook_request_checksum(request);
    nonce = request.nonce;
    return write_exact_file(kShellUiHookRequestPath, request);
}

MainThreadGuardResult install_main_thread_guard() {
    record_stage("legacy_guard_lookup");

    MonoImage* core = open_image(kCoreDll);
    if (!core) {
        log_line("legacy guard: core image unavailable");
        return MainThreadGuardResult::Failed;
    }

    MonoClass* diagnostics = mono_class_from_name_(
        core,
        "Sce.PlayStation.Core.Runtime",
        "Diagnostics");
    MonoMethod* check = diagnostics
        ? mono_class_get_method_from_name_(
              diagnostics,
              "CheckRunningOnMainThread",
              0)
        : nullptr;
    if (!check) {
        log_line("legacy guard: CheckRunningOnMainThread unavailable");
        return MainThreadGuardResult::Failed;
    }

    record_stage("legacy_guard_compile");
    auto* address = static_cast<std::uint8_t*>(mono_compile_method_(check));
    if (!address) {
        log_line("legacy guard: compile failed");
        return MainThreadGuardResult::Failed;
    }

    std::array<std::uint8_t, kAtomicPatchSize> expected{};
    record_stage("legacy_guard_probe");
    if (!probe_hook_bytes(address, expected)) {
        log_line("legacy guard: probe failed method=%p",
                 static_cast<void*>(address));
        return MainThreadGuardResult::Failed;
    }

    if (expected[0] == 0xc3) {
        log_line("legacy guard already disabled method=%p",
                 static_cast<void*>(address));
        record_stage("legacy_guard_ready");
        return MainThreadGuardResult::Installed;
    }

    std::uint64_t nonce = 0;
    record_stage("legacy_guard_request");
    if (!publish_main_thread_guard_request(address, expected, nonce)) {
        log_line("legacy guard: request publish failed method=%p",
                 static_cast<void*>(address));
        return MainThreadGuardResult::Failed;
    }

    ShellUiHookAck ack{};
    record_stage("legacy_guard_ack_wait");
    if (!wait_for_native_hook_ack(getpid(), nonce, ack)) {
        log_line("legacy guard: request timeout method=%p",
                 static_cast<void*>(address));
        return MainThreadGuardResult::Failed;
    }

    if (ack.status ==
        static_cast<std::int32_t>(ShellUiHookStatus::UnsupportedFirmware)) {
        log_line("legacy guard unsupported by controller method=%p",
                 static_cast<void*>(address));
        return MainThreadGuardResult::Unsupported;
    }

    if (ack.phase != static_cast<std::uint8_t>(
            ShellUiHookAckPhase::MainThreadGuard) ||
        ack.status != static_cast<std::int32_t>(ShellUiHookStatus::Success) ||
        ack.verified == 0 || ack.detached == 0 || ack.auth_restored == 0) {
        log_line(
            "legacy guard failed method=%p status=%d read_rc=%d write_rc=%d "
            "verified=%u restored=%u detached=%u auth_restored=%u phase=%u",
            static_cast<void*>(address),
            ack.status,
            ack.read_rc,
            ack.write_rc,
            static_cast<unsigned>(ack.verified),
            static_cast<unsigned>(ack.restored),
            static_cast<unsigned>(ack.detached),
            static_cast<unsigned>(ack.auth_restored),
            static_cast<unsigned>(ack.phase));
        return MainThreadGuardResult::Failed;
    }

    log_line(
        "legacy main-thread guard online method=%p stopped_patch=1 "
        "bytes_changed=1 verified=1",
        static_cast<void*>(address));
    record_stage("legacy_guard_ready");
    return MainThreadGuardResult::Installed;
}

void log_line(const char* fmt, ...) {
    FILE* fp = std::fopen(
        "/system_tmp/padoverlay_shellui.log", "a");
    if (!fp)
        return;

    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(fp, fmt, ap);
    va_end(ap);
    std::fputc('\n', fp);
    std::fclose(fp);
}

MonoImage* open_image(const char* path) {
    if (!g_domain || !mono_domain_assembly_open_ || !mono_assembly_get_image_)
        return nullptr;

    MonoAssembly* assembly = mono_domain_assembly_open_(g_domain, path);
    return assembly ? mono_assembly_get_image_(assembly) : nullptr;
}

MonoObject* invoke(
    MonoMethod* method,
    MonoObject* instance,
    void** args = nullptr) {

    if (!method || !mono_runtime_invoke_)
        return nullptr;

    MonoObject* exception = nullptr;
    MonoObject* result = mono_runtime_invoke_(
        method,
        instance,
        args,
        &exception);

    return exception ? nullptr : result;
}

MonoMethod* property_getter(MonoClass* klass, const char* name) {
    if (!klass)
        return nullptr;
    MonoProperty* property =
        mono_class_get_property_from_name_(klass, name);
    return property ? mono_property_get_get_method_(property) : nullptr;
}

MonoMethod* property_setter(MonoClass* klass, const char* name) {
    if (!klass)
        return nullptr;
    MonoProperty* property =
        mono_class_get_property_from_name_(klass, name);
    return property ? mono_property_get_set_method_(property) : nullptr;
}

MonoObject* managed_target(std::uint32_t handle) {
    return handle != 0 && mono_gchandle_get_target_
        ? mono_gchandle_get_target_(handle)
        : nullptr;
}

bool replace_managed_handle(
    std::uint32_t& handle,
    MonoObject* object) {

    if (!object || !mono_gchandle_new_ || !mono_gchandle_get_target_)
        return false;

    if (handle != 0 && managed_target(handle) == object)
        return true;

    if (handle != 0 && mono_gchandle_free_)
        mono_gchandle_free_(handle);

    handle = mono_gchandle_new_(object, 1);
    return handle != 0 && managed_target(handle) == object;
}

void release_managed_handle(std::uint32_t& handle) {
    if (handle != 0 && mono_gchandle_free_)
        mono_gchandle_free_(handle);
    handle = 0;
}

MonoObject* get_root_widget() {
    MonoObject* game_scene = managed_target(g_game_scene_handle);
    if (!game_scene)
        return nullptr;

    MonoClass* scene = mono_class_from_name_(
        g_pui_image,
        "Sce.PlayStation.PUI.UI2",
        "Scene");
    MonoMethod* getter = property_getter(scene, "RootWidget");
    return getter ? invoke(getter, game_scene) : nullptr;
}

MonoObject* find_widget(MonoObject* root, const char* id) {
    if (!root || !g_pui_image || !g_domain || !mono_string_new_)
        return nullptr;

    MonoClass* klass = mono_class_from_name_(
        g_pui_image,
        "Sce.PlayStation.PUI.UI2",
        "Widget");
    MonoMethod* method = klass
        ? mono_class_get_method_from_name_(klass, "FindWidgetByName", 1)
        : nullptr;
    void* thunk = method ? mono_compile_method_(method) : nullptr;
    if (!thunk)
        return nullptr;

    MonoString* name = mono_string_new_(g_domain, id);
    auto fn = reinterpret_cast<MonoObject* (*)(MonoObject*, MonoString*)>(thunk);
    return fn(root, name);
}

bool remove_child(MonoObject* root, MonoObject* child) {
    if (!root || !child || !g_pui_image || !mono_runtime_invoke_)
        return false;

    MonoClass* klass = mono_class_from_name_(
        g_pui_image,
        "Sce.PlayStation.PUI.UI2",
        "Widget");
    MonoMethod* method = klass
        ? mono_class_get_method_from_name_(klass, "RemoveChild", 1)
        : nullptr;
    if (!method)
        return false;

    void* args[1] = {child};
    MonoObject* exception = nullptr;
    mono_runtime_invoke_(method, root, args, &exception);
    return exception == nullptr;
}

void remove_and_release_handle(MonoObject* root, std::uint32_t& handle) {
    if (handle != 0) {
        MonoObject* target = managed_target(handle);
        if (target && root) {
            remove_child(root, target);
        }
        release_managed_handle(handle);
    }
}

void release_all_sprite_handles(MonoObject* root) {
    remove_and_release_handle(root, g_h_base);
    for (int i = 0; i < 16; ++i)
        remove_and_release_handle(root, g_h_btn[i]);
    for (int i = 0; i < 2; ++i) {
        remove_and_release_handle(root, g_h_stick[i]);
        remove_and_release_handle(root, g_h_stick_l3[i]);
        remove_and_release_handle(root, g_h_trig[i]);
    }
    remove_and_release_handle(root, g_h_touch);
}

void cleanup_orphan_sprites(MonoObject* root) {
    if (!root)
        return;
    const char* names[] = {
        "id_pad_base",
        "id_pad_touch",
    };
    for (const char* n : names) {
        MonoObject* w = find_widget(root, n);
        if (w) remove_child(root, w);
    }
    char id[32];
    for (int i = 0; i < 16; ++i) {
        std::snprintf(id, sizeof id, "id_pad_btn_%d", i);
        MonoObject* w = find_widget(root, id);
        if (w) remove_child(root, w);
    }
    for (int s = 0; s < 2; ++s) {
        std::snprintf(id, sizeof id, "id_pad_stick_%d", s);
        MonoObject* w = find_widget(root, id);
        if (w) remove_child(root, w);
        std::snprintf(id, sizeof id, "id_pad_stick_l3_%d", s);
        w = find_widget(root, id);
        if (w) remove_child(root, w);
    }
    for (int t = 0; t < 2; ++t) {
        std::snprintf(id, sizeof id, "id_pad_trig_%d", t);
        MonoObject* w = find_widget(root, id);
        if (w) remove_child(root, w);
    }
}

bool refresh_game_scene() {
    if (!g_domain || !g_find_scene_method || !mono_string_new_)
        return false;

    MonoDomain* active_domain = mono_domain_get_();
    MonoString* game_path = mono_string_new_(
        active_domain ? active_domain : g_domain,
        "Game");
    void* find_args[1] = {game_path};
    MonoObject* game_scene = invoke(
        g_find_scene_method, nullptr, find_args);
    if (!game_scene)
        return false;

    if (managed_target(g_game_scene_handle) != game_scene) {
        MonoObject* old_root = get_root_widget();
        if (old_root) {
            remove_and_release_handle(old_root, g_label_handle);
            remove_and_release_handle(old_root, g_value_handle);
            release_all_sprite_handles(old_root);
        } else {
            release_managed_handle(g_label_handle);
            release_managed_handle(g_value_handle);
            release_all_sprite_handles(nullptr);
        }
        if (!replace_managed_handle(g_game_scene_handle, game_scene))
            return false;
        g_scene_changed = true;
        g_sprites_built = false;
        log_line(
            "Game ContainerScene refreshed scene=%p handle=%u mode=%s",
            static_cast<void*>(game_scene),
            g_game_scene_handle,
            g_background_render_mode ? "legacy-background" : "update-hook");
    }
    return true;
}

template <typename T>
bool set_property_direct(
    MonoClass* klass,
    MonoObject* instance,
    const char* name,
    T value) {

    MonoMethod* setter = property_setter(klass, name);
    if (!setter)
        return false;

    void* thunk = mono_compile_method_(setter);
    if (!thunk)
        return false;

    auto fn = reinterpret_cast<void (*)(MonoObject*, T)>(thunk);
    fn(instance, value);
    return true;
}

bool set_property_object(
    MonoClass* klass,
    MonoObject* instance,
    const char* name,
    MonoObject* value) {

    MonoMethod* setter = property_setter(klass, name);
    if (!setter)
        return false;

    void* args[1] = {value};
    MonoObject* exception = nullptr;
    mono_runtime_invoke_(setter, instance, args, &exception);
    return exception == nullptr;
}

MonoObject* create_font(int size) {
    MonoClass* klass = mono_class_from_name_(
        g_pui_image,
        "Sce.PlayStation.PUI.UI2",
        "UIFont");
    if (!klass)
        return nullptr;

    MonoObject* boxed = mono_object_new_(g_domain, klass);
    if (!boxed)
        return nullptr;

    void* real = mono_object_unbox_(boxed);
    MonoMethod* ctor = mono_class_get_method_from_name_(klass, ".ctor", 3);
    void* thunk = ctor ? mono_compile_method_(ctor) : nullptr;
    if (!real || !thunk)
        return nullptr;

    auto fn = reinterpret_cast<void (*)(void*, int, int, int)>(thunk);
    fn(real, size, 0, 0);
    return reinterpret_cast<MonoObject*>(real);
}

MonoObject* create_color(float r, float g, float b, float a) {
    MonoClass* klass = mono_class_from_name_(
        g_pui_image,
        "Sce.PlayStation.PUI",
        "UIColor");
    if (!klass)
        return nullptr;

    MonoObject* boxed = mono_object_new_(g_domain, klass);
    if (!boxed)
        return nullptr;

    void* real = mono_object_unbox_(boxed);
    MonoMethod* ctor = mono_class_get_method_from_name_(klass, ".ctor", 4);
    void* thunk = ctor ? mono_compile_method_(ctor) : nullptr;
    if (!real || !thunk)
        return nullptr;

    auto fn = reinterpret_cast<void (*)(void*, float, float, float, float)>(thunk);
    fn(real, r, g, b, a);
    return reinterpret_cast<MonoObject*>(real);
}

MonoObject* create_label(
    const char* name,
    float x,
    float y,
    const char* text,
    MonoObject* font,
    int horizontal_alignment,
    float r,
    float g,
    float b,
    float a) {

    MonoClass* klass = mono_class_from_name_(
        g_pui_image,
        "Sce.PlayStation.PUI.UI2",
        "Label");
    if (!klass)
        return nullptr;

    MonoObject* label = mono_object_new_(g_domain, klass);
    if (!label)
        return nullptr;

    mono_runtime_object_init_(label);

    MonoString* mono_name = mono_string_new_(g_domain, name);
    MonoString* mono_text = mono_string_new_(g_domain, text);
    MonoObject* color = create_color(r, g, b, a);

    bool ok = true;
    ok &= set_property_direct(klass, label, "Name", mono_name);
    ok &= set_property_direct(klass, label, "X", x);
    ok &= set_property_direct(klass, label, "Y", y);
    ok &= set_property_direct(klass, label, "Text", mono_text);
    ok &= set_property_object(klass, label, "Font", font);
    ok &= set_property_direct(
        klass, label, "HorizontalAlignment", horizontal_alignment);
    ok &= set_property_direct(klass, label, "VerticalAlignment", 0);
    ok &= set_property_object(klass, label, "TextColor", color);
    ok &= set_property_direct(klass, label, "FitWidthToText", true);
    ok &= set_property_direct(klass, label, "FitHeightToText", true);

    return ok ? label : nullptr;
}

bool append_child(MonoObject* root, MonoObject* child) {
    MonoClass* klass = mono_class_from_name_(
        g_pui_image,
        "Sce.PlayStation.PUI.UI2",
        "Widget");
    MonoMethod* method = klass
        ? mono_class_get_method_from_name_(klass, "AppendChild", 1)
        : nullptr;
    if (!method || !root || !child)
        return false;

    void* args[1] = {child};
    MonoObject* exception = nullptr;
    mono_runtime_invoke_(method, root, args, &exception);
    return exception == nullptr;
}

bool set_label_text(MonoObject* label, const char* text) {
    if (!label)
        return false;

    MonoClass* klass = mono_class_from_name_(
        g_pui_image,
        "Sce.PlayStation.PUI.UI2",
        "Label");
    MonoString* mono_text = mono_string_new_(g_domain, text);
    return set_property_direct(klass, label, "Text", mono_text);
}

bool write_file_once(const char* path, const unsigned char* data, unsigned len) {
    FILE* fp = std::fopen(path, "wb");
    if (!fp) {
        log_line("pad asset: fopen %s fallo", path);
        return false;
    }
    const size_t w = std::fwrite(data, 1, len, fp);
    std::fclose(fp);
    log_line("pad asset escrito bytes=%zu/%u path=%s", w, len, path);
    return w == len;
}

const char* g_asset_dir = nullptr;

bool write_pad_assets_once() {
    static int state = 0;
    if (state != 0)
        return state == 1;
    static const char* cands[] = {"/Temp", "/download", "/system_tmp", nullptr};
    char path[160];
    for (int c = 0; cands[c]; ++c) {
        bool ok = true;
        for (int i = 0; i < PAD_PNG_COUNT && ok; ++i) {
            std::snprintf(path, sizeof path, "%s/pad_%s", cands[c], pad_pngs[i].file);
            ok = write_file_once(path, pad_pngs[i].data, pad_pngs[i].size);
        }
        if (ok) {
            g_asset_dir = cands[c];
            log_line("pad assets OK en dir=%s", cands[c]);
            state = 1;
            return true;
        }
        log_line("pad assets: dir %s insuficiente", cands[c]);
    }
    state = -1;
    return false;
}

const char* png_uri(int png_index) {
    static char uri[192];
    std::snprintf(uri, sizeof uri, "file://%s/pad_%s",
                  g_asset_dir ? g_asset_dir : "/system_tmp", pad_pngs[png_index].file);
    return uri;
}

struct pad_state_t {
    std::uint32_t buttons;
    std::uint8_t lx, ly, rx, ry, l2, r2;
    std::uint8_t touches;
    std::uint16_t tx, ty;
    bool valid;
};

bool pad_read(pad_state_t* s) {
    s->valid = false;
    if (g_pad_init_state == 0) {
        g_pad_init_state = -1;
        int user = -1;
        int rc = sceUserServiceGetForegroundUser(&user);
        log_line("pad init: GetForegroundUser rc=0x%x user=0x%x", rc, user);
        int h = scePadOpen(user, 0, 0, nullptr);
        log_line("pad init: scePadOpen -> 0x%x", h);
        if (h < 0)
            h = scePadGetHandle(user, 0, 0);
        if (h >= 0) {
            g_pad_handle = h;
            g_pad_init_state = 1;
            log_line("pad init: handle OK = 0x%x", h);
        }
    }
    if (g_pad_init_state != 1)
        return false;

    unsigned char raw[256];
    std::memset(raw, 0, sizeof raw);
    if (scePadReadState(g_pad_handle, raw) < 0)
        return false;

    std::uint32_t buttons;
    std::int32_t connected;
    std::memcpy(&buttons, raw + 0x00, 4);
    std::memcpy(&connected, raw + 0x4c, 4);

    if ((buttons & 0x80000000u) || connected == 0) {
        s->buttons = 0;
        s->lx = s->ly = s->rx = s->ry = 128;
        s->l2 = s->r2 = 0;
        s->touches = 0;
        s->tx = s->ty = 0;
        s->valid = true;
        return true;
    }

    s->buttons = buttons;
    s->lx = raw[0x04]; s->ly = raw[0x05]; s->rx = raw[0x06]; s->ry = raw[0x07];
    s->l2 = raw[0x08]; s->r2 = raw[0x09];
    s->touches = raw[0x34];
    std::memcpy(&s->tx, raw + 0x3c, 2);
    std::memcpy(&s->ty, raw + 0x3e, 2);
    s->valid = true;
    return true;
}

[[maybe_unused]] MonoObject* create_imagebox(
    const char* name,
    float x,
    float y,
    float w,
    float h,
    const char* uri) {

    MonoClass* klass = mono_class_from_name_(
        g_pui_image,
        "Sce.PlayStation.PUI.UI2",
        "ImageBox");
    if (!klass) {
        log_line("ImageBox class not found");
        return nullptr;
    }

    MonoObject* box = mono_object_new_(g_domain, klass);
    if (!box) {
        log_line("ImageBox alloc failed");
        return nullptr;
    }
    mono_runtime_object_init_(box);

    bool ok = true;
    ok &= set_property_direct(klass, box, "Name", mono_string_new_(g_domain, name));
    ok &= set_property_direct(klass, box, "X", x);
    ok &= set_property_direct(klass, box, "Y", y);
    ok &= set_property_direct(klass, box, "Width", w);
    ok &= set_property_direct(klass, box, "Height", h);

    MonoMethod* load = mono_class_get_method_from_name_(klass, "LoadAsync", 1);
    if (!load) {
        log_line("ImageBox.LoadAsync(1) not found");
        return nullptr;
    }
    void* args[1] = {mono_string_new_(g_domain, uri)};
    MonoObject* exception = nullptr;
    mono_runtime_invoke_(load, box, args, &exception);

    log_line("ImageBox created name=%s props_ok=%d load_exc=%p uri=%s",
             name, ok, static_cast<void*>(exception), uri);
    return box;
}

bool apply_packet_on_render(const WirePacket& packet) {
    const auto decoded = decode_wire_packet(packet);
    if (!decoded)
        return false;

    if (!refresh_game_scene())
        return false;

    const OverlayFrame& frame = *decoded;
    MonoObject* root = get_root_widget();
    if (!root) {
        static std::uint64_t last_logged_sequence = 0;
        if (last_logged_sequence != packet.sequence) {
            last_logged_sequence = packet.sequence;
            log_line("UI root unavailable seq=%llu",
                     static_cast<unsigned long long>(packet.sequence));
        }
        return false;
    }

    MonoObject* label = managed_target(g_label_handle);
    MonoObject* value = managed_target(g_value_handle);

    if (!label)
        label = find_widget(root, kLabelId);
    if (!value)
        value = find_widget(root, kValueId);

    if (!label || !value) {
        MonoObject* font = create_font(frame.config.font_size);
        if (!font) {
            log_line("UIFont create failed");
            return false;
        }

        const float x = frame.anchor.x;
        const float y = frame.anchor.y;

        const float value_x = x;

        if (!label) {
            label = create_label(
                kLabelId,
                x,
                y,
                "",
                font,
                1,
                0.702f,
                0.400f,
                1.000f,
                1.000f);
            if (label && !append_child(root, label))
                label = nullptr;
        }

        if (!value) {
            value = create_label(
                kValueId,
                value_x,
                y,
                "--",
                font,
                0,
                1.0f,
                1.0f,
                1.0f,
                1.0f);
            if (value && !append_child(root, value))
                value = nullptr;
        }

        log_line(
            "widgets create seq=%llu label=%p value=%p font=%d x=%.1f y=%.1f",
            static_cast<unsigned long long>(packet.sequence),
            static_cast<void*>(label),
            static_cast<void*>(value),
            frame.config.font_size,
            x,
            y);
    }

    if (!label || !value)
        return false;

    if (!replace_managed_handle(g_label_handle, label) ||
        !replace_managed_handle(g_value_handle, value)) {
        log_line(
            "widget GC handle failed label=%u value=%u",
            g_label_handle,
            g_value_handle);
        return false;
    }

    char text[32]{};
    if (frame.loading)
        std::snprintf(text, sizeof(text), "--");
    else
        std::snprintf(text, sizeof(text), "%d", frame.fps);
    return set_label_text(value, text);
}

void apply_latest_state_on_render() {
    if (!g_runtime_ready.load() || !g_have_packet.load())
        return;

    const std::uint64_t sequence = g_sequence.load();
    if (sequence == g_applied_sequence.load())
        return;

    WirePacket packet{};
    pthread_mutex_lock(&g_packet_lock);
    packet = g_packet;
    pthread_mutex_unlock(&g_packet_lock);

    if (packet.sequence != sequence || !decode_wire_packet(packet))
        return;

    if (apply_packet_on_render(packet))
        g_applied_sequence.store(packet.sequence);
}

using FloatSetter = void (*)(MonoObject*, float);
using BoolSetter = void (*)(MonoObject*, bool);
FloatSetter g_set_x{}, g_set_y{}, g_set_h{}, g_set_op{};
BoolSetter g_set_vis{};
bool g_setters_ready = false;

void init_setters() {
    if (g_setters_ready || !g_pui_image)
        return;
    MonoClass* k = mono_class_from_name_(g_pui_image, "Sce.PlayStation.PUI.UI2", "ImageBox");
    if (!k)
        return;
    auto thunk = [&](const char* n) -> void* {
        MonoMethod* s = property_setter(k, n);
        return s ? mono_compile_method_(s) : nullptr;
    };
    g_set_x = reinterpret_cast<FloatSetter>(thunk("X"));
    g_set_y = reinterpret_cast<FloatSetter>(thunk("Y"));
    g_set_h = reinterpret_cast<FloatSetter>(thunk("Height"));
    g_set_op = reinterpret_cast<FloatSetter>(thunk("Opacity"));
    g_set_vis = reinterpret_cast<BoolSetter>(thunk("Visible"));
    g_setters_ready = g_set_x && g_set_y && g_set_h && g_set_op && g_set_vis;
    log_line("setters cache ready=%d", g_setters_ready ? 1 : 0);
}

inline void set_widget_visible(MonoObject* w, bool vis) { if (g_set_vis) g_set_vis(w, vis); }
inline void set_widget_xy(MonoObject* w, float x, float y) { if (g_set_x) { g_set_x(w, x); g_set_y(w, y); } }
inline void set_widget_height(MonoObject* w, float h) { if (g_set_h) g_set_h(w, h); }
inline void set_widget_opacity(MonoObject* w, float o) { if (g_set_op) g_set_op(w, o); }

float get_widget_float(MonoObject* w, const char* name) {
    MonoClass* klass = mono_class_from_name_(
        g_pui_image, "Sce.PlayStation.PUI.UI2", "Widget");
    MonoMethod* getter = property_getter(klass, name);
    if (!getter)
        return 0.0f;
    MonoObject* r = invoke(getter, w);
    if (!r)
        return 0.0f;
    void* boxed = mono_object_unbox_(r);
    return boxed ? *reinterpret_cast<float*>(boxed) : 0.0f;
}

constexpr float kBaseOpacity = 0.45f;
constexpr float kSpriteOpacity = 0.85f;

MonoObject* make_sprite(MonoObject* root, const char* id, int png, float x, float y,
                        float w, float h, bool visible, float opacity) {
    MonoObject* s = create_imagebox(id, kPadOvX + x, kPadOvY + y, w, h, png_uri(png));
    if (!s)
        return nullptr;
    set_widget_opacity(s, opacity);
    if (!visible)
        set_widget_visible(s, false);
    if (!append_child(root, s)) {
        log_line("sprite %s append fallo", id);
        return nullptr;
    }
    return s;
}

const int kBtnCount = (int)(sizeof(pad_buttons) / sizeof(pad_buttons[0]));

std::uint32_t effective_bit(int i) {
    return pad_buttons[i].bit;
}

void reset_overlay_caches() {
    g_last_buttons = 0xffffffffu;
    g_last_axis[0] = g_last_axis[1] = 0xffff;
    g_last_ring[0] = g_last_ring[1] = false;
    g_last_trig_frac[0] = g_last_trig_frac[1] = -1.0f;
    g_last_touch = false;
}

void draw_pad_overlay() {
    static int calls = 0;
    static bool assets_ready = false;
    if (!g_runtime_ready.load())
        return;
    ++calls;

    if (!refresh_game_scene()) {
        if (g_sprites_built) {
            MonoObject* root = get_root_widget();
            release_all_sprite_handles(root);
            g_sprites_built = false;
            log_line("juego cerrado; overlay en espera");
        }
        return;
    }
    MonoObject* root = get_root_widget();
    if (!root) {
        g_sprites_built = false;
        return;
    }

    if (g_scene_changed) {
        g_scene_changed = false;
        if (g_sprites_built) {
            release_all_sprite_handles(root);
            g_sprites_built = false;
            log_line("escena de juego nueva; reconstruyendo overlay");
        }
    }

    if (!g_sprites_built) {
        cleanup_orphan_sprites(root);
        if (!assets_ready) {
            if (!write_pad_assets_once()) {
                log_line("pad assets no disponibles");
                return;
            }
            init_setters();
            assets_ready = true;
        }

        const float rootH = get_widget_float(root, "Height");
        const float rootW = get_widget_float(root, "Width");
        kPadOvX = 40.0f;
        if (rootH > kPadOvH + 60.0f)
            kPadOvY = rootH - kPadOvH - 40.0f;
        log_line("scene %.0fx%.0f -> overlay en (%.0f,%.0f)", rootW, rootH, kPadOvX, kPadOvY);

        MonoObject* base = make_sprite(root, "id_pad_base", PAD_PNG_BASE, 0, 0, kPadOvW, kPadOvH,
                                       true, kBaseOpacity);
        if (base) replace_managed_handle(g_h_base, base);

        char id[32];
        for (int i = 0; i < kBtnCount; ++i) {
            std::snprintf(id, sizeof id, "id_pad_btn_%d", i);
            MonoObject* s = make_sprite(root, id, pad_buttons[i].png, pad_buttons[i].x * kAS,
                                        pad_buttons[i].y * kAS, pad_buttons[i].w * kAS,
                                        pad_buttons[i].h * kAS, false, kSpriteOpacity);
            if (s) replace_managed_handle(g_h_btn[i], s);
        }
        for (int s = 0; s < 2; ++s) {
            std::snprintf(id, sizeof id, "id_pad_stick_%d", s);
            float w = pad_sticks[s].w * kAS, h = pad_sticks[s].h * kAS;
            float cx = pad_sticks[s].cx * kAS, cy = pad_sticks[s].cy * kAS;
            MonoObject* cap = make_sprite(root, id, PAD_PNG_STICK_CAP,
                                          cx - w / 2, cy - h / 2, w, h, true, kSpriteOpacity);
            if (cap) replace_managed_handle(g_h_stick[s], cap);
            std::snprintf(id, sizeof id, "id_pad_stick_l3_%d", s);
            MonoObject* ring = make_sprite(root, id, PAD_PNG_STICK_CAP_L3,
                                           cx - w / 2, cy - h / 2, w, h, false, kSpriteOpacity);
            if (ring) replace_managed_handle(g_h_stick_l3[s], ring);
        }
        for (int t = 0; t < 2; ++t) {
            std::snprintf(id, sizeof id, "id_pad_trig_%d", t);
            MonoObject* tr = make_sprite(root, id, pad_triggers[t].png, pad_triggers[t].x * kAS,
                                         pad_triggers[t].y * kAS, pad_triggers[t].w * kAS,
                                         pad_triggers[t].h * kAS, false, kSpriteOpacity);
            if (tr) replace_managed_handle(g_h_trig[t], tr);
        }
        MonoObject* dot = make_sprite(root, "id_pad_touch", PAD_PNG_TOUCH_DOT, 0, 0,
                                      pad_touchpad.dot_w * kAS, pad_touchpad.dot_h * kAS,
                                      false, kSpriteOpacity);
        if (dot) replace_managed_handle(g_h_touch, dot);

        g_sprites_built = managed_target(g_h_base) != nullptr;
        reset_overlay_caches();
        log_line("pad_overlay full ready=%d botones=%d", g_sprites_built ? 1 : 0, kBtnCount);
        if (!g_sprites_built)
            return;
    }

    if (calls < 10)
        return;

    pad_state_t p;
    if (!pad_read(&p) || !p.valid)
        return;

    if (p.buttons != g_last_buttons) {
        for (int i = 0; i < kBtnCount; ++i) {
            const std::uint32_t bit = effective_bit(i);
            if (bit == 0)
                continue;
            const bool now = (p.buttons & bit) != 0;
            const bool was = (g_last_buttons & bit) != 0;
            if (now == was)
                continue;
            MonoObject* s = managed_target(g_h_btn[i]);
            if (s) set_widget_visible(s, now);
        }
        g_last_buttons = p.buttons;
    }

    for (int s = 0; s < 2; ++s) {
        const int axv = (s == 0) ? (p.lx | (p.ly << 8)) : (p.rx | (p.ry << 8));
        const bool clicked = (p.buttons & pad_sticks[s].click_bit) != 0;
        const int dxm = ((axv & 0xff) - 128), dym = ((axv >> 8) - 128);
        const bool active = clicked || dxm > 18 || dxm < -18 || dym > 18 || dym < -18;

        if (axv != g_last_axis[s]) {
            const float w = pad_sticks[s].w * kAS, h = pad_sticks[s].h * kAS;
            const float x = kPadOvX + pad_sticks[s].cx * kAS + dxm / 127.0f * kStickTravel - w / 2;
            const float y = kPadOvY + pad_sticks[s].cy * kAS + dym / 127.0f * kStickTravel - h / 2;
            MonoObject* cap = managed_target(g_h_stick[s]);
            MonoObject* ring = managed_target(g_h_stick_l3[s]);
            if (cap) set_widget_xy(cap, x, y);
            if (ring) set_widget_xy(ring, x, y);
            g_last_axis[s] = static_cast<std::uint16_t>(axv);
        }
        if (active != g_last_ring[s]) {
            MonoObject* ring = managed_target(g_h_stick_l3[s]);
            if (ring) set_widget_visible(ring, active);
            g_last_ring[s] = active;
        }
    }

    const std::uint8_t trig[2] = {p.l2, p.r2};
    for (int t = 0; t < 2; ++t) {
        const float frac = trig[t] / 255.0f;
        MonoObject* tr = managed_target(g_h_trig[t]);
        if (!tr)
            continue;
        if (frac < 0.02f) {
            if (g_last_trig_frac[t] >= 0.02f || g_last_trig_frac[t] < 0) { set_widget_visible(tr, false); g_last_trig_frac[t] = 0; }
            continue;
        }
        const float fullh = pad_triggers[t].h * kAS;
        set_widget_visible(tr, true);
        set_widget_height(tr, fullh * frac);
        set_widget_xy(tr, kPadOvX + pad_triggers[t].x * kAS,
                      kPadOvY + pad_triggers[t].y * kAS + fullh * (1.0f - frac));
        g_last_trig_frac[t] = frac;
    }

    MonoObject* dot = managed_target(g_h_touch);
    if (dot) {
        const bool touching = p.touches > 0;
        if (touching) {
            const float u = p.tx / 1920.0f, v = p.ty / 1080.0f;
            set_widget_xy(dot, kPadOvX + (pad_touchpad.x + u * pad_touchpad.w) * kAS - pad_touchpad.dot_w * kAS / 2.0f,
                          kPadOvY + (pad_touchpad.y + v * pad_touchpad.h) * kAS - pad_touchpad.dot_h * kAS / 2.0f);
        }
        if (touching != g_last_touch) { set_widget_visible(dot, touching); g_last_touch = touching; }
    }
}

void application_update_hook(MonoObject* instance) {
    apply_latest_state_on_render();
    draw_pad_overlay();

    if (g_application_update_original)
        g_application_update_original(instance);
}

bool install_update_hook(MonoClass* application_class) {
    record_stage("hook_lookup");
    MonoMethod* update = application_class
        ? mono_class_get_method_from_name_(
              application_class,
              "Update",
              0)
        : nullptr;
    record_stage("hook_compile");
    auto* address = update
        ? static_cast<std::uint8_t*>(mono_compile_method_(update))
        : nullptr;
    if (!address) {
        log_line("Application.Update compile failed");
        return false;
    }

    record_stage("hook_read_start");
    std::array<std::uint8_t, kAtomicPatchSize> expected{};
    if (!probe_hook_bytes(address, expected)) {
        return false;
    }
    record_stage("hook_read_ok");

    static constexpr std::uint8_t kAbsoluteJumpPrefix[6] = {
        0xff, 0x25, 0x00, 0x00, 0x00, 0x00,
    };

    const bool eta_chain =
        std::memcmp(
            expected.data(),
            kAbsoluteJumpPrefix,
            sizeof(kAbsoluteJumpPrefix)) == 0;

    record_stage("hook_prepare");
    std::size_t displaced_size = kAbsoluteJumpSize;
    application_update_t original_call = nullptr;

    if (eta_chain) {
        std::uint64_t previous_destination = 0;
        std::memcpy(
            &previous_destination,
            expected.data() + sizeof(kAbsoluteJumpPrefix),
            sizeof(previous_destination));

        if (previous_destination ==
            reinterpret_cast<std::uint64_t>(&application_update_hook)) {
            if (g_application_update_original &&
                g_hook_install_confirmed) {
                log_line(
                    "Application.Update hook already online method=%p",
                    static_cast<void*>(address));
                return true;
            }
            log_line(
                "Application.Update belongs to another resident renderer "
                "method=%p",
                static_cast<void*>(address));
            return false;
        }

        if (previous_destination < 0x10000ULL ||
            previous_destination >= 0x0000800000000000ULL ||
            previous_destination == reinterpret_cast<std::uint64_t>(address)) {
            log_line(
                "Application.Update etaHEN destination invalid method=%p "
                "destination=%p",
                static_cast<void*>(address),
                reinterpret_cast<void*>(previous_destination));
            return false;
        }

        original_call = reinterpret_cast<application_update_t>(
            previous_destination);
    } else {
        displaced_size = native_patch_length(expected.data());
        if (displaced_size == 0) {
            log_line(
                "Application.Update native prologue rejected "
                "method=%p bytes="
                "%02x%02x%02x%02x%02x%02x%02x%02x"
                "%02x%02x%02x%02x%02x%02x%02x%02x",
                static_cast<void*>(address),
                expected[0], expected[1], expected[2], expected[3],
                expected[4], expected[5], expected[6], expected[7],
                expected[8], expected[9], expected[10], expected[11],
                expected[12], expected[13], expected[14], expected[15]);
            return false;
        }

        record_stage("hook_trampoline");
        if (!prepare_trampoline(
                expected.data(),
                displaced_size,
                address + displaced_size)) {
            log_line(
                "Application.Update trampoline prepare failed method=%p "
                "mode=native-stopped-mdbg displaced=%zu",
                static_cast<void*>(address),
                displaced_size);
            return false;
        }
        original_call = reinterpret_cast<application_update_t>(
            &commonfps_update_trampoline);
    }

    std::array<std::uint8_t, kAtomicPatchSize> desired = expected;
    encode_absolute_jump(
        desired.data(),
        reinterpret_cast<const void*>(&application_update_hook));
    for (std::size_t i = kAbsoluteJumpSize;
         i < displaced_size;
         ++i) {
        desired[i] = 0x90;
    }

    g_application_update_original = original_call;

    std::uint64_t nonce = 0;
    record_stage("hook_request");
    if (!publish_hook_request(
            address,
            expected,
            desired,
            displaced_size,
            original_call,
            nonce)) {
        log_line(
            "Application.Update request publish failed method=%p",
            static_cast<void*>(address));
        return false;
    }

    log_line(
        "Application.Update request ready mode=%s method=%p hook=%p "
        "original=%p displaced=%zu nonce=0x%llx",
        eta_chain ? "etahen-stopped-chain" : "native-stopped-mdbg",
        static_cast<void*>(address),
        reinterpret_cast<void*>(&application_update_hook),
        reinterpret_cast<void*>(original_call),
        displaced_size,
        static_cast<unsigned long long>(nonce));

    ShellUiHookAck ack{};
    record_stage("hook_ack_wait");
    if (!wait_for_native_hook_ack(getpid(), nonce, ack)) {
        log_line(
            "Application.Update request timeout method=%p "
            "nonce=0x%llx",
            static_cast<void*>(address),
            static_cast<unsigned long long>(nonce));
        return false;
    }

    if (ack.status != static_cast<std::int32_t>(
            ShellUiHookStatus::Success) ||
        ack.phase != static_cast<std::uint8_t>(ShellUiHookAckPhase::Patch) ||
        ack.verified == 0 || ack.detached == 0 ||
        ack.auth_restored == 0) {
        log_line(
            "Application.Update request failed method=%p "
            "status=%d read_rc=%d write_rc=%d verified=%u "
            "restored=%u detached=%u auth_restored=%u",
            static_cast<void*>(address),
            ack.status,
            ack.read_rc,
            ack.write_rc,
            static_cast<unsigned>(ack.verified),
            static_cast<unsigned>(ack.restored),
            static_cast<unsigned>(ack.detached),
            static_cast<unsigned>(ack.auth_restored));
        return false;
    }

    log_line(
        "Application.Update hook online method=%p "
        "mode=%s original=%p hook=%p "
        "displaced=%zu stopped_patch=1 verified=1",
        static_cast<void*>(address),
        eta_chain ? "etahen-stopped-chain" : "native-stopped-mdbg",
        reinterpret_cast<void*>(original_call),
        reinterpret_cast<void*>(&application_update_hook),
        displaced_size);
    g_hook_install_confirmed = true;
    record_stage("hook_confirmed");
    return true;
}

}

bool initialize_runtime() {
    if (g_runtime_ready.load())
        return true;

    if (g_hook_attempted)
        return false;

    record_stage("mono_root_call");
    g_domain = mono_get_root_domain_();
    if (!g_domain) {
        log_line("root domain failed");
        return false;
    }
    mono_thread_attach_(g_domain);

    record_stage("mono_attached");

    g_pui_image = open_image(kPuiDll);
    MonoImage* app_system = open_image(kAppSystemDll);
    if (!g_pui_image || !app_system) {
        log_line("managed image open failed pui=%p app=%p",
                 static_cast<void*>(g_pui_image),
                 static_cast<void*>(app_system));
        return false;
    }

    record_stage("managed_images_ready");

    MonoClass* layer_manager = mono_class_from_name_(
        app_system,
        "Sce.Vsh.ShellUI.AppSystem",
        "LayerManager");
    g_find_scene_method = layer_manager
        ? mono_class_get_method_from_name_(
              layer_manager,
              "FindContainerSceneByPath",
              1)
        : nullptr;
    if (!g_find_scene_method) {
        log_line("FindContainerSceneByPath unavailable");
        return false;
    }

    if (!refresh_game_scene()) {
        log_line("Game ContainerScene no disponible al iniciar; overlay armado, "
                 "esperando a que se abra un juego");
    }

    record_stage("scene_ready");

    g_hook_attempted = true;
    MonoClass* application_class = mono_class_from_name_(
        g_pui_image,
        "Sce.PlayStation.PUI",
        "Application");
    record_stage("hook_setup");
    if (application_class && install_update_hook(application_class)) {
        g_background_render_mode = false;
        log_line("renderer backend selected mode=application-update-hook");
    } else {
        log_line("Application.Update hook failed or unavailable, falling back to legacy-guard");
        const MainThreadGuardResult guard = install_main_thread_guard();
        if (guard == MainThreadGuardResult::Installed) {
            g_background_render_mode = true;
            log_line(
                "renderer backend selected mode=legacy-background-pui "
                "Application.Update=untouched");
        } else {
            log_line(
                "renderer backend selection failed; fail-closed without "
                "Application.Update write");
            return false;
        }
    }

    g_runtime_ready.store(true);
    log_line(
        "runtime ready pid=%d scene_handle=%u gc_mode=pinned backend=%s",
        getpid(),
        g_game_scene_handle,
        g_background_render_mode
            ? "legacy-background-pui"
            : "application-update-hook");
    return true;
}

bool initialize_receiver() {
    if (g_receiver_fd >= 0)
        return true;

    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        log_line("receiver socket failed");
        return false;
    }

    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    timeval receive_timeout{};
    receive_timeout.tv_sec = 0;
    receive_timeout.tv_usec = 33000;
    if (setsockopt(
            fd,
            SOL_SOCKET,
            SO_RCVTIMEO,
            &receive_timeout,
            sizeof(receive_timeout)) < 0) {
        log_line("receiver timeout setup failed");
        close(fd);
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kDefaultIpcPort);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(
            fd,
            reinterpret_cast<sockaddr*>(&addr),
            sizeof(addr)) < 0) {
        log_line("receiver bind failed port=%u", kDefaultIpcPort);
        close(fd);
        return false;
    }

    g_receiver_fd = fd;
    log_line(
        "receiver ready port=%u mode=persistent-main-thread "
        "timeout_ms=2000 stale=1",
        kDefaultIpcPort);
    return true;
}

[[noreturn]] void run_receiver_loop() {
    for (;;) {
        WirePacket packet{};
        (void)recv(g_receiver_fd, &packet, sizeof(packet), 0);

        if (g_background_render_mode)
            draw_pad_overlay();
    }
}

}
