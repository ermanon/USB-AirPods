"""Run production recovery/timeline/packet policies as actual ARM machine code."""
from pathlib import Path
import struct
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / 'build/test-runtime'))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR

toolchain = Path.home() / '.pico-sdk/toolchain/14_2_Rel1/bin'
source = (root / 'src/btstack/btstack_avdtp_source.c').read_text(encoding='utf-8')
negative = sys.argv[1] if len(sys.argv) > 1 else ''
if negative not in ('', 'queue-guard', 'packet-frames'):
    raise SystemExit('Optional negative control: queue-guard or packet-frames')

def extract_function(signature, file_source=source):
    begin = file_source.index(signature + ' {') if signature + ' {' in file_source else file_source.index(signature + '{')
    body = file_source.index('{', begin)
    level = 1
    end = body + 1
    while level:
        if file_source[end] == '{': level += 1
        if file_source[end] == '}': level -= 1
        end += 1
    return file_source[begin:end] + '\n'

def extract_typedef(name):
    end = source.index('} ' + name + ';') if '} ' + name + ';' in source else source.index('}  ' + name + ';')
    begin = source.rfind('typedef struct {', 0, end)
    return source[begin:source.index(';', end) + 1] + '\n'

# Extract complete production functions unchanged; the harness supplies IRQ
# scheduling and queue primitives, then tests slot ownership at each preemption.
generated = (root / 'tests/queue_harness_prefix.c').read_text()
generated += extract_typedef('a2dp_media_sending_context_t')
generated += extract_typedef('audio_slot_t')
begin = source.index('static audio_slot_t slot_pool')
end = source.index('static void audio_slot_report', begin)
globals_text = source[begin:end]
generated += globals_text[:globals_text.index('static uint32_t eld_next_timestamp')]
generated += extract_function('void audio_slot_queue_init(void)')
generated += extract_function('void audio_slot_queue_configure_with_count(uint16_t samples_per_slot, uint8_t slot_count)')
generated += extract_function('void audio_slot_push_samples(const int16_t *src, uint16_t stereo_pair_count)')
generated += extract_function('static bool audio_slot_pop(uint8_t *slot_idx)')
generated += extract_function('static void audio_packet_reset(a2dp_media_sending_context_t *context)')
generated += (root / 'tests/send_harness_prefix.c').read_text()
generated += extract_function('static uint8_t audio_payload_send(uint8_t *payload, uint16_t size)')
generated += (root / 'tests/send_harness_cases.c').read_text()
generated += (root / 'tests/queue_harness_cases.c').read_text()
generated += (root / 'tests/case_identity_prefix.c').read_text()
case_source = (root / 'src/btstack/airpods_case.c').read_text(encoding='utf-8')
generated += extract_function('static bool case_address_matches(const bd_addr_t address)', case_source)
generated += (root / 'tests/case_identity_cases.c').read_text()
generated += (root / 'tests/negotiation_harness_prefix.c').read_text()
generated += extract_function('void set_next_capablity_and_start_stream()')
generated += (root / 'tests/negotiation_harness_cases.c').read_text()
if negative == 'queue-guard':
    signature = 'void audio_slot_queue_configure_with_count(uint16_t samples_per_slot, uint8_t slot_count)'
    function = extract_function(signature)
    broken = function.replace('uint32_t flags = save_and_disable_interrupts();', 'uint32_t flags = irq_enabled;')
    assert broken != function
    generated = generated.replace(function, broken)
elif negative == 'packet-frames':
    signature = 'static void audio_packet_reset(a2dp_media_sending_context_t *context)'
    function = extract_function(signature)
    broken = function.replace('context->codec_num_frames = 0;', '')
    assert broken != function
    generated = generated.replace(function, broken)
generated_path = root / 'build/recovery-test-harness.c'
generated_path.write_text(generated, encoding='utf-8')
elf = root / 'build/recovery-policy-test.elf'
binary = root / 'build/recovery-policy-test.bin'
data_binary = root / 'build/recovery-policy-test-data.bin'
rijndael = Path.home() / '.pico-sdk/sdk/2.1.1/lib/btstack/3rd-party/rijndael'
subprocess.run([str(toolchain / 'arm-none-eabi-gcc.exe'), '-mcpu=cortex-m3',
    '-mthumb', '-O2', '-ffreestanding', '-fno-builtin', '-ffunction-sections',
    '-nostdlib', '-Wall', '-Wextra', '-Werror', '-Wno-unused-variable',
    '-DWITH_QUEUE_TESTS', '-I', str(root / 'tests'), '-I', str(rijndael),
    '-T', str(root / 'tests/arm_test.ld'), str(generated_path),
    str(rijndael / 'rijndael.c'), '-lgcc', '-o', str(elf)], check=True)
subprocess.run([str(toolchain / 'arm-none-eabi-objcopy.exe'), '-O', 'binary',
    '--only-section=.text', str(elf), str(binary)], check=True)
subprocess.run([str(toolchain / 'arm-none-eabi-objcopy.exe'), '-O', 'binary',
    '--only-section=.data', str(elf), str(data_binary)], check=True)
uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
uc.mem_map(0x10000, 0x10000)
uc.mem_map(0x20000000, 0x10000)
uc.mem_map(0x30000, 0x1000)
uc.mem_write(0x10000, binary.read_bytes())
uc.mem_write(0x20000100, data_binary.read_bytes())
uc.reg_write(UC_ARM_REG_SP, 0x2000fff0)
uc.reg_write(UC_ARM_REG_LR, 0x30001)
uc.emu_start(0x10001, 0x30000, count=5_000_000)
checks, failures, line = struct.unpack('<III', uc.mem_read(0x20000000, 12))
message = f'ARM policy checks={checks}, failures={failures}, first_failure_line={line}, negative_control={negative or "none"}'
log_name = f'recovery-negative-{negative}.log' if negative else 'recovery-policy-test.log'
(root / 'build' / log_name).write_text(message + '\n', encoding='utf-8')
print(message)
if negative:
    if not failures: raise SystemExit('Negative control did not detect the injected bug')
elif failures or checks < 100:
    raise SystemExit(1)
