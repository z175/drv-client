#include "portable_executable.hpp"

PIMAGE_NT_HEADERS64 portable_executable::GetNtHeaders(void* image_base)
{
	const auto dos_header = reinterpret_cast<PIMAGE_DOS_HEADER>(image_base);

	if (dos_header->e_magic != IMAGE_DOS_SIGNATURE)
		return nullptr;

	const auto nt_headers = reinterpret_cast<PIMAGE_NT_HEADERS64>(reinterpret_cast<uint64_t>(image_base) + dos_header->e_lfanew);

	if (nt_headers->Signature != IMAGE_NT_SIGNATURE)
		return nullptr;

	return nt_headers;
}

portable_executable::vec_relocs portable_executable::GetRelocs(void* image_base)
{
	const PIMAGE_NT_HEADERS64 nt_headers = GetNtHeaders(image_base);

	if (!nt_headers)
		return {};

	const auto reloc_data_directory = nt_headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];

	if (!reloc_data_directory.VirtualAddress || !reloc_data_directory.Size)
		return {};

	vec_relocs relocs;

	auto current_base_relocation = reinterpret_cast<PIMAGE_BASE_RELOCATION>(reinterpret_cast<uint64_t>(image_base) + reloc_data_directory.VirtualAddress);
	const auto reloc_end = reinterpret_cast<uint64_t>(current_base_relocation) + reloc_data_directory.Size;

	while (current_base_relocation->VirtualAddress && current_base_relocation->VirtualAddress < reloc_end && current_base_relocation->SizeOfBlock)
	{
		RelocInfo reloc_info;

		reloc_info.address = reinterpret_cast<uint64_t>(image_base) + current_base_relocation->VirtualAddress;
		reloc_info.item = reinterpret_cast<uint16_t*>(reinterpret_cast<uint64_t>(current_base_relocation) + sizeof(IMAGE_BASE_RELOCATION));
		reloc_info.count = (current_base_relocation->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(uint16_t);

		relocs.push_back(reloc_info);

		current_base_relocation = reinterpret_cast<PIMAGE_BASE_RELOCATION>(reinterpret_cast<uint64_t>(current_base_relocation) + current_base_relocation->SizeOfBlock);
	}

	return relocs;
}

portable_executable::vec_imports portable_executable::GetImports(void* image_base)
{
	const PIMAGE_NT_HEADERS64 nt_headers = GetNtHeaders(image_base);

	if (!nt_headers)
		return {};

	const auto import_data_directory = nt_headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];

	if (!import_data_directory.VirtualAddress || !import_data_directory.Size)
		return {};

	vec_imports imports;

	auto current_import_descriptor = reinterpret_cast<PIMAGE_IMPORT_DESCRIPTOR>(reinterpret_cast<uint64_t>(image_base) + import_data_directory.VirtualAddress);

	while (current_import_descriptor->FirstThunk)
	{
		ImportInfo import_info;

		import_info.module_name = std::string(reinterpret_cast<char*>(reinterpret_cast<uint64_t>(image_base) + current_import_descriptor->Name));

		auto current_first_thunk = reinterpret_cast<PIMAGE_THUNK_DATA64>(reinterpret_cast<uint64_t>(image_base) + current_import_descriptor->FirstThunk);
		auto current_originalFirstThunk = reinterpret_cast<PIMAGE_THUNK_DATA64>(reinterpret_cast<uint64_t>(image_base) + current_import_descriptor->OriginalFirstThunk);

		while (current_originalFirstThunk->u1.Function)
		{
			ImportFunctionInfo import_function_data;

			auto thunk_data = reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(reinterpret_cast<uint64_t>(image_base) + current_originalFirstThunk->u1.AddressOfData);

			import_function_data.name = thunk_data->Name;
			import_function_data.address = &current_first_thunk->u1.Function;

			import_info.function_datas.push_back(import_function_data);

			++current_originalFirstThunk;
			++current_first_thunk;
		}

		imports.push_back(import_info);
		++current_import_descriptor;
	}

	return imports;
}

uint32_t portable_executable::GetSectionProtection(uint32_t section_characteristics)
{
	uint32_t result = 0;

	if (section_characteristics & IMAGE_SCN_MEM_NOT_CACHED)
		result |= PAGE_NOCACHE;

	if (section_characteristics & IMAGE_SCN_MEM_EXECUTE)
	{
		if (section_characteristics & IMAGE_SCN_MEM_READ)
		{
			if (section_characteristics & IMAGE_SCN_MEM_WRITE)
				result |= PAGE_EXECUTE_READWRITE;
			else
				result |= PAGE_EXECUTE_READ;
		}
		else
		{
			if (section_characteristics & IMAGE_SCN_MEM_WRITE)
				result |= PAGE_EXECUTE_WRITECOPY;
			else
				result |= PAGE_EXECUTE;
		}
	}
	else
	{
		if (section_characteristics & IMAGE_SCN_MEM_READ)
		{
			if (section_characteristics & IMAGE_SCN_MEM_WRITE)
				result |= PAGE_READWRITE;
			else
				result |= PAGE_READONLY;
		}
		else
		{
			if (section_characteristics & IMAGE_SCN_MEM_WRITE)
				result |= PAGE_WRITECOPY;
			else
				result |= PAGE_NOACCESS;
		}
	}

	return result;
}