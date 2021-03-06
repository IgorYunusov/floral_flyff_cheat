#include "floral_flyff.h"
#include "../h/losu.h"
#include "../h/summoner.h"
#include "../h/texts.h"

#include <windows.h>
#include <stdio.h>
#include <vector>
#include <cmath>
#include <ctime>

const unsigned long floral_flyff::OFFSET_SELECT = 0x20;                    // type = 4 bytes
const unsigned long floral_flyff::OFFSET_X = 0x188;                        // type = float
const unsigned long floral_flyff::OFFSET_LVL = 0x79C + 8 + 0x10;           // type = 4 bytes
const unsigned long floral_flyff::OFFSET_IS_DEAD = 0x900 + 8 + 0x10;       // 255 = alive, 6 = dead, type = 1 byte
const unsigned long floral_flyff::OFFSET_TYPE_PET = 0x7EC + 8 + 0x10;      // type = 1 byte, 19 = pet, 0 = npc, 3 = aibatt
const unsigned long floral_flyff::OFFSET_NAME = 0x1890 + 8 + 0x10;         // char array
const unsigned long floral_flyff::OFFSET_ID = 0x3F4;                       // 4 byte int
const unsigned long floral_flyff::OFFSET_MONEY = 0x1884 + 8 + 0x10;        // 4 byte array int

//////////////////// threads \\\\\\\\\\\\\\\\\\\\

unsigned long __stdcall floral_flyff::_thread_select_target(void *t) {
    bool killed;
    flyff *f;
    flyff::key k;
    flyff::targetInfo ti;
    time_t time_selected;
    unsigned long bad_target;

    f = ((flyff *)t); // main class for every client
    killed = true;

    f->localPlayer->select(0);

    for (;; Sleep(100)) {
        // deal with killing
        if (f->localPlayer->get_select() == 0) {
            if (killed == false) {
                if (f->bot->get_kill_to_home() > 0 && f->bot->killed_count >= f->bot->get_kill_to_home()) {
                    f->localPlayer->teleport_to_saved_pos();
                    f->bot->killed_count = 0;
                    Sleep(1000);
                }

                f->bot->killed_count++;

                // setting bot statis text to searching
                SetWindowText((HWND)f->ui->get_hwnd_noti(), (char *)texts::noti_bot_searching_target);
            }

            // select target if any
            ti = f->bot->get_closest_target_in_view();
            if (ti.base != 0 && ti.base != bad_target) {
                printf("selecting closest target: %08X\n", ti.base);
                f->localPlayer->select(ti.base);

                // get time we select target
                time_selected = time(0);

                if (f->bot->get_kill_to_home() > 0)
                    f->localPlayer->teleport_to_target(ti);

                killed = false;

                // setting bot status text to attacking
                SetWindowText((HWND)f->ui->get_hwnd_noti(), (char *)texts::noti_bot_attacking_target);
            }

            // rotate cam
            f->bot->thread_uing = true;
            PostMessage((HWND)f->ui->get_hwnd(), WM_KEYDOWN, VK_RIGHT, MapVirtualKey(VK_RIGHT, MAPVK_VK_TO_VSC));
            Sleep(50);
            PostMessage((HWND)f->ui->get_hwnd(), WM_KEYUP, VK_RIGHT, MapVirtualKey(VK_RIGHT, MAPVK_VK_TO_VSC));
            Sleep(50);
            f->bot->thread_uing = false;
        }
        else {
            // get key to use
            if (f->bot->get_key(&k)) {
                // send key to window
                f->bot->thread_uing = true;
                f->localPlayer->attack();
                Sleep(50);
                f->bot->thread_uing = false;
            }

            // check time we have time to kill target
            if (f->bot->get_reselect_after() > 0) {
                time_t now = time(0);

                if (time_selected + f->bot->get_reselect_after() < now) {
                    // if we passed time we had to kill target then select 0
                    bad_target = f->localPlayer->get_select();
                    f->localPlayer->select(0);

                    printf("couldn't killd in %d seconds, reselcting target\n", f->bot->get_reselect_after());
                }
            }
        }
    }

    return 0;
}

unsigned long __stdcall _thread_perin_converter(void *t) {
    flyff *f;

    f = ((flyff *)t); // main class for every client

    for (;; Sleep(1000)) {
        // check does we need to convert to perin
        //if (f.get_perin_convert_spam()) { // no need to check since 9.2.2017
        if (f->localPlayer->get_money() >= 100000000)
            f->enable_perin_convert_spam(true);
        else f->enable_perin_convert_spam(false);
        //}
    }
}

//////////////////// pricate functions \\\\\\\\\\\\\\\\\\\\

float floral_flyff::get_hyp(flyff *f, flyff::targetInfo ti) {
    float x = 0,
        z = 0;

    if (OFFSET_X != 0) {
        ZwReadVirtualMemory(f->localPlayer->handle, (void *)(f->localPlayer->get_me() + OFFSET_X), &x, 4, 0);
        ZwReadVirtualMemory(f->localPlayer->handle, (void *)(f->localPlayer->get_me() + OFFSET_X + 8), &z, 4, 0);
    }

    return sqrt((x - ti.x) * (x - ti.x) + (z - ti.z) * (z - ti.z));
}

//////////////////// class contructors \\\\\\\\\\\\\\\\\\\\

floral_flyff::floral_flyff(void) {}

floral_flyff::floral_flyff(void *handle, unsigned long base_addr, unsigned long base_size) {
    load(handle, base_addr, base_size);
}

floral_flyff::floral_flyff(unsigned long pid) {
    void *handle;
    unsigned long base, base_size;

    printf("Floral Flyff --- opening proces\n");
    handle = VZwOpenProcess(pid);

    if (handle) {
        // get base and size if available
        base = get_module(pid, "Neuz.exe", &base_size);

        if (base == 0) {
            // set variables21
            base = 0x00AB0000; //get_module(pid, "Neuz.exe", &base_size); // get_module does not work on wine staging 2.9
            base_size = 0x00917000; // this and base from immunity debugger
        }

        error_string = nullptr;
        load(handle, base, base_size);
    }
    else error_string = (char *)texts::error_open_process;
}

void floral_flyff::load(void *handle, unsigned long base_addr, unsigned long base_size) {
    unsigned long addr;
    char buf[256];

    printf("Searcing for Floral Flyff ... ");

    if (search(handle, base_addr, base_size, "Floral Flyff", 12, 1) != 0) {
        printf(" | Done\n");

        _base_addr = base_addr;
        _handle = handle;

        // old 0x66EDE4 (dif: +20)
        // updated 8.26.2017 0x66EE04 (dif: +2190)
        // updated 10.2.2017
        _select_addr = base_addr + 0x670F94;
        // updated 8.26.2017 0x668D88 + 0x20 (dif: +2190)
        // updated 10.2.2017
        _maxInView_addr = base_addr + 0x66AF38;
        // updated 8.26.2017 0x65E5F0 + 0x20 (dif: +2190)
        // updated 10.2.2017
        _targetBase_addr = base_addr + 0x6607A0;
        // updated 8.26.2017 0x659A48 + 0x20 (dif: +2194)
        // updated 10.2.2017
        _me_addr = base_addr + 0x65BBFC;
        // old 0x66FDA0 (dif: -0x00980000)
        // updated 8.27.2017 0x00FC7AFC - 0x00980000 (dif: -2050)
        // updated 10.3.2017
        _range_nr_addr = base_addr + 0x00FC7AFC - 0x00980000 - 0x2050;
        // old 0x2A6161 (dif: +150)
        // updated 8.26.2017 0x2A62B1 (dif: +A60)
        // updated 10.3.2017
        _range_addr = base_addr + 0x2A6D11;
        // old 0x2A654A (dif: +150)
        // updated 8.26.2017 0x2A654A + 0x150 (dif: +A60)
        // updated 10.3.2017
        _range_all_addr = base_addr + 0x2A654A + 0x150 + 0xA60;

        // old 0x6400BC (dif: -0x00980000)
        // update 8.27.2017 0xFC00BC - 0x00980000 (dif: -2050)
        // updated 10.3.2017
        _no_collision_addr = base_addr + 0x64210C;

        // updated 9.2.2017
        _perin_convert_spam_write_addr = base_addr + 0x249096; // old 0x249016(dif: +80)
        // updated 9.2.2017
        _perin_convert_spam_ecx = base_addr + 0x66B628; // old 0x66B608(dif: +20)

        // some initializings
        init_range();
        // need update
        //init_perin_convert_spam();

        // { - waiting _select_addr to point
        printf("waiting when _select_addr points ... ");
        for (addr = 0; !addr; Sleep(20))
            ZwReadVirtualMemory(_handle, (void *)(_select_addr), &addr, 4, 0);
        printf("%08X | Done\n", addr + OFFSET_SELECT);
        // end of waiting _select_add to point - }

            // fillin virtual vars
            localPlayer = new ci_localPlayer();
            bot = new ci_bot();

            // filling with few vars
            localPlayer->parent = this;
            localPlayer->handle = _handle;
            localPlayer->select_addr = _select_addr;
            localPlayer->me_addr = _me_addr;
            localPlayer->no_collision_addr = _no_collision_addr;
            localPlayer->range_nr_addr = _range_nr_addr;

            localPlayer->max_range = 99999999.f;

            bot->parent = this;
            bot->handle = _handle;
            bot->maxInView_addr = _maxInView_addr;
            bot->targetBase_addr = _targetBase_addr;

        // { - waiting _me_addr to point
        printf("waiting when _me_addr points ... ");
        for (addr = 0; !addr; Sleep(20))
            addr = localPlayer->get_me();
        printf("%08X | Done\n", addr);
        // end of waiting _me_addr to point - }

        // printing some local vars
        printf("local money: %d\n", localPlayer->get_money());
        localPlayer->get_name(buf);
        printf("local name: %s\n", buf);

        // nulling some vars
        bot->h_select_thread = nullptr;
        bot->set_kill_to_home(0);
        bot->killed_count = 0;
        bot->set_reselect_after(0);
        memset(localPlayer->saved_pos, '\x00', 12);
    } 
    // if we havent found flyff we give nothing
    else {
        printf(" | Failed\n");
        error_string = (char *)texts::error_flyff_not_found;

        // close function
        return;
    }
}

//////////////////// localPlayer \\\\\\\\\\\\\\\\\\\\
// ------------------------------------------------- gets
void floral_flyff::ci_localPlayer::get_name(char *name) {
    memcpy(&*name, "Floral Flyff: can't get name", 29);

	if (OFFSET_NAME != 0)
		ZwReadVirtualMemory(handle, (void *)(get_me() + OFFSET_NAME), &*name + 14, 255 -14, 0);
}

unsigned int floral_flyff::ci_localPlayer::get_money() {
    unsigned int money = 0;

    if (OFFSET_MONEY != 0)
        ZwReadVirtualMemory(handle, (void *)(get_me() + OFFSET_MONEY), &money, 4, 0);

    return money;
}

unsigned int floral_flyff::ci_localPlayer::get_hp() {
	unsigned int hp = 0;

	//if (OFFSET_HP != 0)
	//	ZwReadVirtualMemory(handle, (void *)(get_me() + OFFSET_MONEY), &money, 4, 0);

	return hp;
}

unsigned long floral_flyff::ci_localPlayer::get_me() {
    unsigned long value = 0;
    ZwReadVirtualMemory(handle, (void *)(me_addr), &value, 4, 0);
    return value;
}

unsigned long floral_flyff::ci_localPlayer::get_select() {
    unsigned long pointed = 0;

    if (OFFSET_SELECT != 0) {
        ZwReadVirtualMemory(handle, (void *)(select_addr), &pointed, 4, 0);
        ZwReadVirtualMemory(handle, (void *)(pointed + OFFSET_SELECT), &pointed, 4, 0);
    }

    return pointed;
}

void floral_flyff::ci_localPlayer::get_location(unsigned char *loc) {
     memcpy(loc, saved_pos, 12);
}

bool floral_flyff::ci_localPlayer::get_no_collision() {
    bool collision;
    ZwReadVirtualMemory(handle, (void *)(no_collision_addr), &collision, 1, 0);
    return !collision;
}


// ------------------------------------------------- sets
void floral_flyff::ci_localPlayer::save_location(unsigned char *loc) {
    // if loc is null then getting local player pos, else given loc
    if (loc == nullptr)
        if (OFFSET_X != 0)
            ZwReadVirtualMemory(handle, (void *)(get_me() + OFFSET_X), &saved_pos, 12, 0);
        else memcpy(saved_pos, loc, 12);

        *(float *)(saved_pos + 4) += 2.f;
}

void floral_flyff::ci_localPlayer::set_no_collision(bool state) {
    if (state == true)
        ZwWriteVirtualMemory(handle, (void *)(no_collision_addr), "\x00", 1, 0, true);
    else
        ZwWriteVirtualMemory(handle, (void *)(no_collision_addr), "\x01", 1, 0, true);
}

float floral_flyff::ci_localPlayer::set_range(float f) {
    // set range number
    ZwWriteVirtualMemory(handle, (void *)(range_nr_addr), &f, 4, 0);

    // return actual value that we wrote
    return f;
}

// ------------------------------------------------- something to do
void floral_flyff::ci_localPlayer::teleport_to_saved_pos() {
    if (OFFSET_X != 0)
        ZwWriteVirtualMemory(handle, (void *)(get_me() + OFFSET_X), &saved_pos, 12, 0);
}

void floral_flyff::ci_localPlayer::teleport_to_target(targetInfo target) {
    unsigned char pos[12];

    if (OFFSET_X != 0) {
        ZwReadVirtualMemory(handle, (void *)(target.base + OFFSET_X), &pos, 12, 0);
        *(float *)(pos + 4) += 2.f;
        ZwWriteVirtualMemory(handle, (void *)(get_me() + OFFSET_X), &pos, 12, 0);
    }
}

void floral_flyff::ci_localPlayer::select(unsigned long target) {
    unsigned long pointed = 0;

    if (OFFSET_SELECT != 0) {
        ZwReadVirtualMemory(handle, (void *)(select_addr), &pointed, 4, 0);
        ZwWriteVirtualMemory(handle, (void *)(pointed + OFFSET_SELECT), &target, 4, 0);
    }
}

void floral_flyff::ci_localPlayer::attack() {
    key k;
    
    if (parent->bot->get_key(&k)) {
        PostMessage((HWND)parent->ui->get_hwnd(), WM_KEYDOWN, k.code, MapVirtualKey(k.code, MAPVK_VK_TO_VSC));
        Sleep(50);
        PostMessage((HWND)parent->ui->get_hwnd(), WM_KEYUP, k.code, MapVirtualKey(k.code, MAPVK_VK_TO_VSC));
    }

}

//////////////////// bot \\\\\\\\\\\\\\\\\\\\
// ------------------------------------------------- gets
flyff::targetInfo floral_flyff::ci_bot::get_closest_target_in_view() {
    unsigned long maxInView;
    unsigned long target;
    unsigned long type;
    unsigned long lvl;
    unsigned long type_pet;
    unsigned char is_dead;

    targetInfo closest_ti;

    maxInView = 0;
    closest_ti = targetInfo();
    closest_ti.hyp = 99999999.f;

    ZwReadVirtualMemory(handle, (void *)(maxInView_addr), &maxInView, 4, 0);

    //printf("maxInView: %d\n", maxInView);

    for (unsigned long i = 1; i < maxInView; i++) {
        target = 0;
        type = 0;
        lvl = 0;
        type_pet = 0;
        is_dead = 0;

        ZwReadVirtualMemory(handle, (void *)(i * 4 + targetBase_addr), &target, 4, 0);
        ZwReadVirtualMemory(handle, (void *)(target + 8), &type, 4, 0);
        ZwReadVirtualMemory(handle, (void *)(target + OFFSET_LVL), &lvl, 4, 0);
        ZwReadVirtualMemory(handle, (void *)(target + OFFSET_TYPE_PET), &type_pet, 4, 0);
        ZwReadVirtualMemory(handle, (void *)(target + OFFSET_IS_DEAD), &is_dead, 1, 0);

        //printf("base: %08X\ntarget: %08X\ntype: %d\nlvl: %d\nis_dead: %d\npet: %d\n", 
        //    i * 4 + targetBase_addr, target, type, lvl, type_pet, is_dead);

        if (type == 18 && lvl >= target_lvl_begin && lvl <= target_lvl_end && type_pet != 19 && is_dead == 255) {
            targetInfo ti;
            ZwReadVirtualMemory(handle, (void *)(target + OFFSET_X), &ti.x, 4, 0);
            ZwReadVirtualMemory(handle, (void *)(target + OFFSET_X + 4), &ti.y, 4, 0);
            ZwReadVirtualMemory(handle, (void *)(target + OFFSET_X + 8), &ti.z, 4, 0);
            ti.hyp = get_hyp(this->parent, ti);

            if (ti.hyp < closest_ti.hyp) {
                ti.base = target;
                ti.lvl = lvl;
                closest_ti = ti;
            }
        }
    }

    return closest_ti;
}

bool floral_flyff::ci_bot::get_key(key *k) {
    if (keys.size() > 0) {
        *k = keys[0];
        return true;
    }
    return false;
}


// ------------------------------------------------- sets
void floral_flyff::ci_bot::add_update_attack_key(key k, bool remove) {
    if (remove) {
        for (int i = 0; i < keys.size(); i++) {
            if (keys[i].code == k.code && keys[i].priority == k.priority) {
                keys.erase(keys.begin() + i - 1);
                break;
            }
        }
    } else keys.push_back(k);
}

// ------------------------------------------------- something to do
bool floral_flyff::ci_bot::run() {
    // save new position when bot enables
    parent->localPlayer->save_location();
    // resets vars
    killed_count = 0;

    // running target selecting, killing thread
    h_select_thread = CreateThread(0, 0, _thread_select_target, this->parent, 0, 0);
    // set bot status text to created
    SendMessage((HWND)parent->ui->get_hwnd_noti(), WM_SETTEXT, 0, (LPARAM)texts::noti_bot_created);

    return get_run();
}

void floral_flyff::ci_bot::stop() {
    // waiting for thread to finish all keypresses
    for (Sleep(50); thread_uing; Sleep(50));
    // terminating target selecting and killing thread and nulling vars
    TerminateThread(h_select_thread, 0);
    h_select_thread = nullptr;
    // set bot status text to idle
    SetWindowText((HWND)parent->ui->get_hwnd_noti(), (char *)texts::noti_bot_idle);
}


// initializings
void floral_flyff::init_range() {
    // enabling range for everyone
    ZwWriteVirtualMemory(_handle, (void *)(_range_all_addr), (void *)"\x90\x90", 2, 0, true);

    // force to use set range
    ZwWriteVirtualMemory(_handle, (void *)(_range_addr), (void *)"\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90", 12, 0, true);
    ZwWriteVirtualMemory(_handle, (void *)(_range_addr + 12 + 2), &_range_nr_addr, 4, 0, true);
}

void floral_flyff::init_perin_convert_spam() {
    ZwWriteVirtualMemory(_handle, (void *)(_perin_convert_spam_write_addr),
        // old "\xEB\x35\x68\xC8\xCC\x00\x00\xB9\x08\xB6\x9A\x00\xE8\x59\x19\x23\x00\xEB\x24\x90", 20, 0, true);
        // updated 9.2.2017
        "\xEB\x35\x68\xC8\xCC\x00\x00\xB9\x08\xB6\x9A\x00\xE8\x29\x1A\x23\x00\xEB\x24\x90", 20, 0, true);
    ZwWriteVirtualMemory(_handle, (void *)(_perin_convert_spam_write_addr + 8), &_perin_convert_spam_ecx, 4, 0, true);
}


//////////////////// miscs \\\\\\\\\\\\\\\\\\\\
// ------------------------------------------------- sets
void floral_flyff::set_perin_convert_spam(bool state) {
    _use_perin_convert_spam = state;

    if (state)
        // make thread to check penya nad convert if needed
        _h_select_thread = CreateThread(0, 0, _thread_perin_converter, this, 0, 0);
    else
        // kill thread if checkbox is unchecked
        TerminateThread(_h_select_thread, 0);
}

// ------------------------------------------------- gets
bool floral_flyff::get_perin_convert_spam() {
    return _use_perin_convert_spam;
}

// ------------------------------------------------- something to do
void floral_flyff::enable_perin_convert_spam(bool state) {
    unsigned char bytes[2];

    ZwReadVirtualMemory(_handle, (void *)(_perin_convert_spam_write_addr), &bytes, 2, 0);

    if (state == true) {
        if (memcpy(bytes, "\x90\x90", 2) != 0)
            ZwWriteVirtualMemory(_handle, (void *)(_perin_convert_spam_write_addr), "\x90\x90", 2, 0, true);
    }
    else if (memcpy(bytes, "\xEB\x35", 2) != 0)
        ZwWriteVirtualMemory(_handle, (void *)(_perin_convert_spam_write_addr), "\xEB\x35", 2, 0, true);
}
