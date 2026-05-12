#include "emccmd_slot.hh"
#include "cmd_msg.hh"

#include <mutex>
#include <condition_variable>
#include <atomic>
#include <cstring>

// Serial numbers start high to avoid collisions with NML serials
// during the transition period.
static std::atomic<int> gmi_serial{1000000};

// Command slot: one command at a time, synchronized between
// the submitting Go goroutine and the milltask main loop.
static std::mutex slot_mtx;
static std::condition_variable cv_cmd_ready;   // Go → milltask: command available
static std::condition_variable cv_cmd_done;    // milltask → Go: processing complete

static char slot_buf[4096];
static size_t slot_size = 0;       // >0 means command pending
static bool slot_done = false;     // true means milltask finished processing
static int slot_result = 0;        // RCS_STATUS from milltask

void emccmd_slot_init()
{
    std::lock_guard<std::mutex> lk(slot_mtx);
    slot_size = 0;
    slot_done = false;
    slot_result = 0;
    gmi_serial.store(1000000);
}

int emccmd_submit(RCS_CMD_MSG *msg, size_t msg_size)
{
    // Serialize concurrent callers — only one command in flight.
    std::unique_lock<std::mutex> lk(slot_mtx);

    // Assign serial number.
    msg->serial_number = gmi_serial.fetch_add(1);

    // Place command in slot.
    if (msg_size > sizeof(slot_buf))
        return -1;  // shouldn't happen
    std::memcpy(slot_buf, msg, msg_size);
    slot_size = msg_size;
    slot_done = false;

    // Wake milltask.
    cv_cmd_ready.notify_one();

    // Block until milltask calls emccmd_slot_done().
    cv_cmd_done.wait(lk, [] { return slot_done; });

    return slot_result;
}

size_t emccmd_slot_take(void *buf, size_t buf_size)
{
    std::lock_guard<std::mutex> lk(slot_mtx);
    if (slot_size == 0)
        return 0;

    if (slot_size > buf_size)
        return 0;

    std::memcpy(buf, slot_buf, slot_size);
    size_t sz = slot_size;
    slot_size = 0;  // consumed
    return sz;
}

void emccmd_slot_done(int status)
{
    std::lock_guard<std::mutex> lk(slot_mtx);
    slot_result = status;
    slot_done = true;
    cv_cmd_done.notify_one();
}
