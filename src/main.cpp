extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Query(const SKSE::QueryInterface* a_skse, SKSE::PluginInfo* a_info)
{
#ifndef DEBUG
	auto sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
#else
	auto path = logger::log_directory();
	if (!path) {
		return false;
	}

	*path /= Version::PROJECT;
	*path += ".log"sv;
	auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
#endif

	auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));

#ifndef DEBUG
	log->set_level(spdlog::level::trace);
#else
	log->set_level(spdlog::level::info);
	log->flush_on(spdlog::level::info);
#endif

	spdlog::set_default_logger(std::move(log));
	spdlog::set_pattern("%g(%#): [%^%l%$] %v"s);

	logger::info(FMT_STRING("{} v{}"), Version::PROJECT, Version::NAME);

	a_info->infoVersion = SKSE::PluginInfo::kVersion;
	a_info->name = Version::PROJECT.data();
	a_info->version = Version::MAJOR;

	if (a_skse->IsEditor()) {
		logger::critical("Loaded in editor, marking as incompatible"sv);
		return false;
	}

	const auto ver = a_skse->RuntimeVersion();
	if (ver < SKSE::RUNTIME_1_5_39) {
		logger::critical(FMT_STRING("Unsupported runtime version {}"), ver.string());
		return false;
	}

	return true;
}

float change_max_power(float power, void* functor)
{
	auto menu = (RE::CraftingSubMenus::EnchantConstructMenu*)((char*)functor - 0x1A8);
	if (auto _soulgem = menu->selected.soulGem.get(); _soulgem && _soulgem->data && _soulgem->data->object) {
		if (auto soulgem = _soulgem->data->object->As<RE::TESSoulGem>()) {
			// 0..5
			auto soul = static_cast<uint32_t>(soulgem->GetContainedSoul());

			return power * 0.2f * soul;
		}
	}

	return power;
}

// change ench power (=> max mag) depending on soulgem
void Hook()
{
	// SkyrimSE.exe+868AEF
	uintptr_t ret_addr = REL::ID(50366).address() + 0xef;

	struct Code : Xbyak::CodeGenerator
	{
		Code(uintptr_t ret_addr, uintptr_t func)
		{
			// [rsp+68h+power] = power
			// rdi = CreateEffectFunctor
			// ans in xmm2

			movss(xmm0, ptr[rsp + 0x68 + 0x8]);
			mov(rdx, rdi);

			mov(rax, func);
			call(rax);
			movss(xmm2, xmm0);
			
			mov(rax, ret_addr);
			jmp(rax);
		}
	} xbyakCode{ ret_addr, uintptr_t(change_max_power) };
	add_trampoline<6, 50366, 0xe9>(&xbyakCode);  // SkyrimSE.exe+868AE9
}

static void SKSEMessageHandler(SKSE::MessagingInterface::Message* message)
{
	switch (message->type) {
	case SKSE::MessagingInterface::kDataLoaded:
		Hook();

		break;
	}
}

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
	auto g_messaging = reinterpret_cast<SKSE::MessagingInterface*>(a_skse->QueryInterface(SKSE::LoadInterface::kMessaging));
	if (!g_messaging) {
		logger::critical("Failed to load messaging interface! This error is fatal, plugin will not load.");
		return false;
	}

	logger::info("loaded");

	SKSE::Init(a_skse);
	SKSE::AllocTrampoline(1 << 10);

	g_messaging->RegisterListener("SKSE", SKSEMessageHandler);

	return true;
}
