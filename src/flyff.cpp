#include "h/flyff.h"
#include "h/losu.h"
#include "h/summoner.h"
#include "h/texts.h"

#include <windows.h>
#include <stdio.h>
#include <vector>
#include <cmath>

unsigned long OFFSET_SELECT = 0x20;             // type = 4 bytes
unsigned long OFFSET_X = 0x188;                 // type = float
unsigned long OFFSET_LVL = 0x79C;               // type = 4 bytes
unsigned long OFFSET_IS_DEAD = 0x900;           // 255 = alive, 6 = dead, type = 1 byte
unsigned long OFFSET_TYPE_PET = 0x7EC;          // type = 1 byte, 19 = pet
unsigned long OFFSET_NAME = 0x1890;             // char array

// no class functinos
unsigned long __stdcall _thread_select_target(void *t) {
    flyff f = *((flyff *)t); // main class for every client
    flyff::key k;
    
    flyff::targetInfo ti;
    f.select(0);
    
    for (;; Sleep(100)) {
        if (f.getSelect() == 0) {
            // rotate cam
            PostMessage((HWND)f.get_hwnd(), WM_KEYDOWN, VK_LEFT, MapVirtualKey(VK_LEFT, MAPVK_VK_TO_VSC));
            Sleep(20);
            PostMessage((HWND)f.get_hwnd(), WM_KEYUP, VK_LEFT, MapVirtualKey(VK_LEFT, MAPVK_VK_TO_VSC));
            
            ti = f.getClosestTargetInView();
            
            if (ti.base != 0) {
                printf("closest target: %08X\n", ti.base);
                if (ti.base != 0) {
                    f.select(ti.base);
                }   
            }
        } else {
            // get key to use
            if (f.getKey(&k)) {
                // send key to window
                Sleep(20);
                PostMessage((HWND)f.get_hwnd(), WM_KEYDOWN, k.code, MapVirtualKey(k.code, MAPVK_VK_TO_VSC));
                Sleep(20);
                PostMessage((HWND)f.get_hwnd(), WM_KEYUP, k.code, MapVirtualKey(k.code, MAPVK_VK_TO_VSC));
            }
        }
    }
    
    return 0;
}


flyff::flyff(void) {}

flyff::flyff(void *handle, unsigned long base_addr, unsigned long base_size) {
    load(handle, base_addr, base_size);	
}

flyff::flyff(unsigned long pid) {
    void *handle;
    unsigned long base, base_size;
    
    handle = VZwOpenProcess(pid);
        
    if (handle) {
        // get base and size if available
        base = get_module(pid, "Neuz.exe", &base_size);

        if (base == 0) {
            // set variables
            base = 0x00400000; //get_module(pid, "Neuz.exe", &base_size); // get_module does not work on wine staging 2.9
            base_size = 0x00917000; // this and base from immunity debugger
        }
        
        error_string = nullptr;
        load(handle, base, base_size);
    } else error_string = (char *)texts::error_open_process;
}

void flyff::load(void *handle, unsigned long base_addr, unsigned long base_size) {
    unsigned long addr;
    char name[256];
    
    ZwReadVirtualMemory(handle, (void *)(0x6615D1B), &name, 11, 0);
    
    // null some
    _vars._h_select_thread = nullptr;
    
    if (memcmp("floralflyff", name, 11) == 0) {
        _vars._base_addr = base_addr;
        _vars._handle = handle;
        
        
        _vars._select_addr = base_addr + 0x66EDE4;
        _vars._maxInView_addr = base_addr + 0x668D88;
        _vars._targetBase_addr = base_addr + 0x65E5F0;
        _vars._me_addr = base_addr + 0x659A48;
        
        
        _vars._range_nr_addr = base_addr + 0x66FDA0;
        _vars._range_addr = base_addr + 0x2A6161;
        _vars._range_all_addr = base_addr + 0x2A654A;
        
        // { - waiting _select_addr to point
        printf("waiting when _select_addr points ... ");
        for (addr = 0; !addr; Sleep(20))
            ZwReadVirtualMemory(_vars._handle, (void *)(_vars._select_addr), &addr, 4, 0);
        printf("%08X | Done\n", addr + OFFSET_SELECT);
        // end of waiting _select_add to point - }

        
        // { - waiting _me_addr to point
        printf("waiting when _me_addr points ... ");
        for (addr = 0; !addr; Sleep(20))
            ZwReadVirtualMemory(_vars._handle, (void *)(_vars._me_addr), &addr, 4, 0);
        printf("%08X | Done\n", addr);
        // end of waiting _me_addr to point - }
    }
}

bool flyff::run() {
    if (_vars._h_select_thread == nullptr) {
        _vars._h_select_thread = CreateThread(0, 0, _thread_select_target, this, 0, 0);
        return false;
    } return true;
}
void flyff::stop() {
    TerminateThread(_vars._h_select_thread, 0);
    _vars._h_select_thread = nullptr;
}

void flyff::set_hwnd(void *hwnd) { _vars._hwnd = hwnd; }
void *flyff::get_hwnd() { return _vars._hwnd; }

void flyff::get_local_name(char *name) {
    ZwReadVirtualMemory(_vars._handle, (void *)(getMe() + OFFSET_NAME), &*name, 255, 0);
}

void flyff::select(unsigned long target) {
	unsigned long pointed = 0;
	ZwReadVirtualMemory(_vars._handle, (void *)(_vars._select_addr), &pointed, 4, 0);
	ZwWriteVirtualMemory(_vars._handle, (void *)(pointed + OFFSET_SELECT), &target, 4, 0);
}

unsigned long flyff::getSelect() {
	unsigned long pointed = 0;
	ZwReadVirtualMemory(_vars._handle, (void *)(_vars._select_addr), &pointed, 4, 0);
	ZwReadVirtualMemory(_vars._handle, (void *)(pointed + OFFSET_SELECT), &pointed, 4, 0);
	return pointed;
}

unsigned long flyff::getMe() {
	unsigned long value = 0;
	ZwReadVirtualMemory(_vars._handle, (void *)(_vars._me_addr), &value, 4, 0);
	return value;
}

float flyff::getHyp(targetInfo ti) {
	float x, z;
	ZwReadVirtualMemory(_vars._handle, (void *)(getMe() + OFFSET_X), &x, 4, 0);
	ZwReadVirtualMemory(_vars._handle, (void *)(getMe() + OFFSET_X +8), &z, 4, 0);
	return sqrt((x - ti.x) * (x - ti.x) + (z - ti.z) * (z - ti.z));
}

flyff::targetInfo flyff::getClosestTargetInView() {
	unsigned long maxInView;
    unsigned long target;
    unsigned long type;
    unsigned long lvl;
    unsigned char is_dead;
    unsigned long type_pet;
    
	targetInfo closest_ti;
    
    maxInView = 0;
    closest_ti = targetInfo();
	closest_ti.hyp = 99999999;

	ZwReadVirtualMemory(_vars._handle, (void *)(_vars._maxInView_addr), &maxInView, 4, 0);
    
    //printf("maxInView: %d\n", maxInView);
    
	for (unsigned long i = 1; i < maxInView; i++) {
		target = 0;
        type = 0;
        lvl = 0;
        is_dead = 0;
        type_pet = 0;

		ZwReadVirtualMemory(_vars._handle, (void *)(i * 4 + _vars._targetBase_addr), &target, 4, 0);
		ZwReadVirtualMemory(_vars._handle, (void *)(target + 8), &type, 4, 0);
		ZwReadVirtualMemory(_vars._handle, (void *)(target + OFFSET_LVL), &lvl, 4, 0);
		ZwReadVirtualMemory(_vars._handle, (void *)(target + OFFSET_IS_DEAD), &is_dead, 1, 0);
		ZwReadVirtualMemory(_vars._handle, (void *)(target + OFFSET_TYPE_PET), &type_pet, 1, 0);

        //printf("base: %08X\ntarget: %08X\ntype: %d\nlvl: %d\nis_dead: %d\n", i * 4 + _vars._targetBase_addr, target, type, lvl, is_dead);
        
		if (type == 18 && lvl >= _vars._target_lvl_begin && lvl <= _vars._target_lvl_end && is_dead == 255 && type_pet != 19) {
			targetInfo ti;
			ZwReadVirtualMemory(_vars._handle, (void *)(target + OFFSET_X), &ti.x, 4, 0);
			ZwReadVirtualMemory(_vars._handle, (void *)(target + OFFSET_X +4), &ti.y, 4, 0);
			ZwReadVirtualMemory(_vars._handle, (void *)(target + OFFSET_X +8), &ti.z, 4, 0);
			ti.hyp = getHyp(ti);

			if (ti.hyp < closest_ti.hyp) {
				ti.base = target;
				ti.lvl = lvl;
				closest_ti = ti;
			}
		}
	}
	
	return closest_ti;
}


void flyff::addUpdateAttackKey(unsigned char key, float priority, bool remove) {
    flyff::key k;
    k.code = key;
    k.priority = priority;
    
    _vars._keys.clear();
    _vars._keys.push_back(k);
}

bool flyff::getKey(flyff::key *k) {
    if (_vars._keys.size() > 0) {
        *k = _vars._keys[0];
        return true;
    }
    return false;
}

void flyff::attack() {
    
}

void flyff::set_target_lvls(int begin, int end) {
    if (begin != -1) _vars._target_lvl_begin = begin;
    if (end != -1) _vars._target_lvl_end = end;
}

void flyff::get_target_lvls(int *begin, int *end) {
    *begin = _vars._target_lvl_begin;
    *end = _vars._target_lvl_end;
}


void flyff::teleport_to_target(unsigned long target) {
	unsigned char pos[12];
	//ReadProcessMemory(_handle, (LPCVOID)(target + OFFSET_X), &pos, 12, 0);
	//*(float *)(pos +4) += 3.f;
	//WriteProcessMemory(_handle, (LPVOID)(getMe() + OFFSET_X), &pos, 12, 0);
}

unsigned char saved_pos[12];
void flyff::save_location() {
	//ReadProcessMemory(_handle, (LPCVOID)(getMe() + OFFSET_X), &saved_pos, 12, 0);
	//*(float *)(saved_pos +4) += 3.f;
}

void flyff::teleport_to_saved_pos() {
	//WriteProcessMemory(_handle, (LPVOID)(getMe() + OFFSET_X), &saved_pos, 12, 0);
}

bool bo_set_range = false;
void flyff::set_range(float f) {
    if (bo_set_range == false) {
        // enabling range for everyone
        ZwWriteVirtualMemory(_vars._handle, (void *)(_vars._range_all_addr), (void *)"\x90\x90", 2, 0, true);
            
        // force to use set range
        ZwWriteVirtualMemory(_vars._handle, (void *)(_vars._range_addr), (void *)"\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90", 12, 0, true);
        ZwWriteVirtualMemory(_vars._handle, (void *)(_vars._range_addr +12 +2), &_vars._range_nr_addr, 4, 0, true);
        
        bo_set_range = true;
    }
    
    // set range number
    ZwWriteVirtualMemory(_vars._handle, (void *)(_vars._range_nr_addr), &f, 4, 0);
}
