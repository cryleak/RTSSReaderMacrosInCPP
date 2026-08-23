#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Updater.h"

#include <Windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace {

constexpr wchar_t kApiUrl[] = L"https://api.github.com/repos/cryleak/RTSSReaderMacrosInCPP/releases/latest";

struct HttpHandles {
	HINTERNET session = nullptr;
	HINTERNET connection = nullptr;
	HINTERNET request = nullptr;

	~HttpHandles() {
		if (request) WinHttpCloseHandle(request);
		if (connection) WinHttpCloseHandle(connection);
		if (session) WinHttpCloseHandle(session);
	}
};

std::string winHttpError(const char* operation, DWORD code = GetLastError()) {
	return std::string(operation) + " failed (" + std::to_string(code) + ").";
}

std::wstring asciiWide(const char* value) {
	std::wstring result;
	for (; value && *value; ++value) result += static_cast<wchar_t>(static_cast<unsigned char>(*value));
	return result;
}

bool crackUrl(const std::wstring& url, std::wstring& host, std::wstring& path, INTERNET_PORT& port, bool& secure, std::string& error) {
	std::wstring mutableUrl = url;
	URL_COMPONENTS components{};
	components.dwStructSize = sizeof(components);
	components.dwSchemeLength = static_cast<DWORD>(-1);
	components.dwHostNameLength = static_cast<DWORD>(-1);
	components.dwUrlPathLength = static_cast<DWORD>(-1);
	components.dwExtraInfoLength = static_cast<DWORD>(-1);
	if (!WinHttpCrackUrl(mutableUrl.data(), 0, 0, &components)) {
		error = winHttpError("WinHttpCrackUrl");
		return false;
	}
	host.assign(components.lpszHostName, components.dwHostNameLength);
	path.assign(components.lpszUrlPath, components.dwUrlPathLength);
	if (components.dwExtraInfoLength) path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
	if (path.empty()) path = L"/";
	port = components.nPort;
	secure = components.nScheme == INTERNET_SCHEME_HTTPS;
	return !host.empty();
}

bool beginGet(const std::wstring& url, HttpHandles& handles, DWORD& status, std::string& error, bool apiRequest) {
	std::wstring host;
	std::wstring path;
	INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
	bool secure = true;
	if (!crackUrl(url, host, path, port, secure, error)) return false;

	const std::wstring userAgent = L"RTSSReaderMacros/" + asciiWide(Updater::kCurrentVersion);
	handles.session = WinHttpOpen(userAgent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
		WINHTTP_NO_PROXY_BYPASS, 0);
	if (!handles.session) {
		error = winHttpError("WinHttpOpen");
		return false;
	}
	if (!WinHttpSetTimeouts(handles.session, 10000, 10000, 15000, 15000)) {
		error = winHttpError("WinHttpSetTimeouts");
		return false;
	}
	handles.connection = WinHttpConnect(handles.session, host.c_str(), port, 0);
	if (!handles.connection) {
		error = winHttpError("WinHttpConnect");
		return false;
	}
	handles.request = WinHttpOpenRequest(handles.connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
		WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0);
	if (!handles.request) {
		error = winHttpError("WinHttpOpenRequest");
		return false;
	}
	const wchar_t* headers = apiRequest
		? L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\nCache-Control: no-cache\r\n"
		: L"Accept: application/octet-stream\r\nCache-Control: no-cache\r\n";
	if (!WinHttpSendRequest(handles.request, headers, static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
		!WinHttpReceiveResponse(handles.request, nullptr)) {
		error = winHttpError("GitHub request");
		return false;
	}
	DWORD statusSize = sizeof(status);
	if (!WinHttpQueryHeaders(handles.request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX)) {
		error = winHttpError("WinHttpQueryHeaders");
		return false;
	}
	return true;
}

bool readResponse(HttpHandles& handles, std::string& body, std::string& error) {
	std::vector<char> buffer;
	for (;;) {
		DWORD available = 0;
		if (!WinHttpQueryDataAvailable(handles.request, &available)) {
			error = winHttpError("WinHttpQueryDataAvailable");
			return false;
		}
		if (!available) return true;
		buffer.resize(available);
		DWORD read = 0;
		if (!WinHttpReadData(handles.request, buffer.data(), available, &read)) {
			error = winHttpError("WinHttpReadData");
			return false;
		}
		body.append(buffer.data(), read);
		if (!read) return true;
	}
}

bool downloadToFile(const std::wstring& url, const std::wstring& path, std::string& error) {
	HttpHandles handles;
	DWORD status = 0;
	if (!beginGet(url, handles, status, error, false)) return false;
	if (status < 200 || status >= 300) {
		error = "GitHub returned HTTP " + std::to_string(status) + ".";
		return false;
	}

	HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		error = winHttpError("CreateFile");
		return false;
	}
	std::vector<char> buffer;
	bool success = true;
	for (;;) {
		DWORD available = 0;
		if (!WinHttpQueryDataAvailable(handles.request, &available)) {
			error = winHttpError("WinHttpQueryDataAvailable");
			success = false;
			break;
		}
		if (!available) break;
		buffer.resize(available);
		DWORD read = 0;
		if (!WinHttpReadData(handles.request, buffer.data(), available, &read)) {
			error = winHttpError("WinHttpReadData");
			success = false;
			break;
		}
		if (!read) break;
		DWORD written = 0;
		if (!WriteFile(file, buffer.data(), read, &written, nullptr) || written != read) {
			error = winHttpError("WriteFile");
			success = false;
			break;
		}
	}
	CloseHandle(file);
	return success;
}

std::string lowerAscii(std::string value) {
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return value;
}

std::string readJsonString(std::string_view object, std::string_view key) {
	const std::string quotedKey = "\"" + std::string(key) + "\"";
	size_t position = object.find(quotedKey);
	if (position == std::string_view::npos) return {};
	position = object.find(':', position + quotedKey.size());
	if (position == std::string_view::npos) return {};
	position = object.find('"', position + 1);
	if (position == std::string_view::npos) return {};
	std::string value;
	for (++position; position < object.size(); ++position) {
		char c = object[position];
		if (c == '"') return value;
		if (c != '\\' || position + 1 >= object.size()) {
			value += c;
			continue;
		}
		char escaped = object[++position];
		switch (escaped) {
		case '"': value += '"'; break;
		case '\\': value += '\\'; break;
		case '/': value += '/'; break;
		case 'b': value += '\b'; break;
		case 'f': value += '\f'; break;
		case 'n': value += '\n'; break;
		case 'r': value += '\r'; break;
		case 't': value += '\t'; break;
		case 'u':
			if (position + 4 < object.size()) position += 4;
			break;
		default: value += escaped; break;
		}
	}
	return {};
}

size_t jsonObjectEnd(std::string_view json, size_t start) {
	int depth = 0;
	bool quoted = false;
	bool escaped = false;
	for (size_t i = start; i < json.size(); ++i) {
		const char c = json[i];
		if (quoted) {
			if (escaped) escaped = false;
			else if (c == '\\') escaped = true;
			else if (c == '"') quoted = false;
			continue;
		}
		if (c == '"') quoted = true;
		else if (c == '{') ++depth;
		else if (c == '}' && --depth == 0) return i;
	}
	return std::string_view::npos;
}

std::string normalizeVersion(std::string value) {
	while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
	while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
	if (!value.empty() && (value.front() == 'v' || value.front() == 'V')) value.erase(value.begin());
	return value;
}

std::vector<int> versionParts(std::string_view value) {
	std::vector<int> parts;
	for (size_t i = 0; i < value.size();) {
		if (!std::isdigit(static_cast<unsigned char>(value[i]))) {
			++i;
			continue;
		}
		int part = 0;
		while (i < value.size() && std::isdigit(static_cast<unsigned char>(value[i]))) {
			if (part < 100000000) part = part * 10 + (value[i] - '0');
			else part = 1000000000;
			++i;
		}
		parts.push_back(part);
	}
	return parts;
}

int compareVersions(std::string_view left, std::string_view right) {
	const auto leftParts = versionParts(left);
	const auto rightParts = versionParts(right);
	if (leftParts.empty() || rightParts.empty()) return 0;
	const size_t count = std::max(leftParts.size(), rightParts.size());
	for (size_t i = 0; i < count; ++i) {
		const int leftPart = i < leftParts.size() ? leftParts[i] : 0;
		const int rightPart = i < rightParts.size() ? rightParts[i] : 0;
		if (leftPart != rightPart) return leftPart < rightPart ? -1 : 1;
	}
	return 0;
}

std::wstring utf8ToWide(const std::string& value) {
	if (value.empty()) return {};
	const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
	if (length <= 0) return {};
	std::wstring result(static_cast<size_t>(length), L'\0');
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), length);
	return result;
}

std::wstring releaseAssetUrl(std::string_view json) {
	size_t cursor = json.find("\"assets\"");
	if (cursor == std::string_view::npos) return {};
	cursor = json.find('[', cursor);
	if (cursor == std::string_view::npos) return {};
	std::wstring fallback;
	for (++cursor; cursor < json.size();) {
		const size_t objectStart = json.find('{', cursor);
		if (objectStart == std::string_view::npos) break;
		const size_t objectEnd = jsonObjectEnd(json, objectStart);
		if (objectEnd == std::string_view::npos) break;
		const std::string_view object = json.substr(objectStart, objectEnd - objectStart + 1);
		const std::string name = readJsonString(object, "name");
		const std::wstring url = utf8ToWide(readJsonString(object, "browser_download_url"));
		if (!url.empty() && lowerAscii(name) == "rtssreadermacros.exe") return url;
		if (fallback.empty() && !url.empty() && lowerAscii(name).ends_with(".exe")) fallback = url;
		cursor = objectEnd + 1;
		if (cursor >= json.size() || json[cursor] == ']') break;
	}
	return fallback;
}

std::wstring modulePath() {
	std::vector<wchar_t> buffer(260);
	for (;;) {
		const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
		if (!length) return {};
		if (length < buffer.size() - 1) return std::wstring(buffer.data(), length);
		buffer.resize(buffer.size() * 2);
	}
}

std::wstring tempPath(const wchar_t* suffix) {
	std::vector<wchar_t> buffer(260);
	DWORD length = GetTempPathW(static_cast<DWORD>(buffer.size()), buffer.data());
	if (!length) return {};
	if (length >= buffer.size()) {
		buffer.resize(length + 1);
		length = GetTempPathW(static_cast<DWORD>(buffer.size()), buffer.data());
		if (!length || length >= buffer.size()) return {};
	}
	std::wstring path(buffer.data(), length);
	if (!path.empty() && path.back() != L'\\') path += L'\\';
	return path + L"RTSSReaderMacros-update-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
		std::to_wstring(GetTickCount64()) + suffix;
}

bool validExecutable(const std::wstring& path, std::string& error) {
	WIN32_FILE_ATTRIBUTE_DATA attributes{};
	if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes)) {
		error = winHttpError("GetFileAttributesEx");
		return false;
	}
	const unsigned long long size = (static_cast<unsigned long long>(attributes.nFileSizeHigh) << 32) | attributes.nFileSizeLow;
	if (size < 65536) {
		error = "The downloaded release asset is too small to be an executable.";
		return false;
	}
	HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		error = winHttpError("CreateFile");
		return false;
	}
	char signature[2]{};
	DWORD read = 0;
	const bool result = ReadFile(file, signature, sizeof(signature), &read, nullptr) && read == sizeof(signature) &&
		signature[0] == 'M' && signature[1] == 'Z';
	CloseHandle(file);
	if (!result) error = "The downloaded release asset is not a Windows executable.";
	return result;
}

std::wstring powerShellQuote(const std::wstring& value) {
	std::wstring result = L"'";
	for (wchar_t c : value) {
		if (c == L'\'') result += L"''";
		else result += c;
	}
	result += L"'";
	return result;
}

std::string wideToUtf8(const std::wstring& value) {
	if (value.empty()) return {};
	const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
	if (length <= 0) return {};
	std::string result(static_cast<size_t>(length), '\0');
	WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), length, nullptr, nullptr);
	return result;
}

bool writeUtf8File(const std::wstring& path, const std::wstring& content, std::string& error) {
	const std::string utf8 = "\xEF\xBB\xBF" + wideToUtf8(content);
	HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		error = winHttpError("CreateFile");
		return false;
	}
	DWORD written = 0;
	const DWORD length = static_cast<DWORD>(utf8.size());
	const bool result = WriteFile(file, utf8.data(), length, &written, nullptr) && written == length;
	CloseHandle(file);
	if (!result) error = winHttpError("WriteFile");
	return result;
}

bool launchReplacement(const std::wstring& source, const std::wstring& target, const std::wstring& helper, std::string& error) {
	const std::wstring script =
		L"$source = " + powerShellQuote(source) + L"\r\n"
		L"$target = " + powerShellQuote(target) + L"\r\n"
		L"$helper = " + powerShellQuote(helper) + L"\r\n"
		L"$targetPid = " + std::to_wstring(GetCurrentProcessId()) + L"\r\n"
		L"$deadline = [DateTime]::UtcNow.AddSeconds(45)\r\n"
		L"while ([DateTime]::UtcNow -lt $deadline) {\r\n"
		L"  if (-not (Get-Process -Id $targetPid -ErrorAction SilentlyContinue)) { break }\r\n"
		L"  Start-Sleep -Milliseconds 250\r\n"
		L"}\r\n"
		L"$replaced = $false\r\n"
		L"for ($attempt = 0; $attempt -lt 80; $attempt++) {\r\n"
		L"  try { Move-Item -LiteralPath $source -Destination $target -Force -ErrorAction Stop; $replaced = $true; break } catch { Start-Sleep -Milliseconds 250 }\r\n"
		L"}\r\n"
		L"if ($replaced) { Start-Process -FilePath $target }\r\n"
		L"Remove-Item -LiteralPath $source -Force -ErrorAction SilentlyContinue\r\n"
		L"Remove-Item -LiteralPath $helper -Force -ErrorAction SilentlyContinue\r\n";
	if (!writeUtf8File(helper, script, error)) return false;

	const std::wstring powershell = L"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
	std::wstring commandLine = L"\"" + powershell + L"\" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File \"" + helper + L"\"";
	std::vector<wchar_t> command(commandLine.begin(), commandLine.end());
	command.push_back(L'\0');
	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION process{};
	if (!CreateProcessW(powershell.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
		nullptr, nullptr, &startup, &process)) {
		error = winHttpError("CreateProcess");
		return false;
	}
	CloseHandle(process.hThread);
	CloseHandle(process.hProcess);
	return true;
}

} // namespace

namespace Updater {

UpdateInfo checkForUpdate() {
	UpdateInfo info;
	const std::wstring url = std::wstring(kApiUrl) + L"?cache=" + std::to_wstring(GetTickCount64());
	HttpHandles handles;
	DWORD status = 0;
	std::string error;
	if (!beginGet(url, handles, status, error, true)) {
		info.error = std::move(error);
		return info;
	}
	if (status < 200 || status >= 300) {
		info.error = "GitHub returned HTTP " + std::to_string(status) + ".";
		return info;
	}
	std::string body;
	if (!readResponse(handles, body, error)) {
		info.error = std::move(error);
		return info;
	}
	const std::string tag = normalizeVersion(readJsonString(body, "tag_name"));
	if (tag.empty() || versionParts(tag).empty()) {
		info.error = "The latest GitHub release did not contain a valid version tag.";
		return info;
	}
	info.latestVersion = tag;
	info.assetUrl = releaseAssetUrl(body);
	info.available = compareVersions(tag, kCurrentVersion) > 0 && !info.assetUrl.empty();
	info.ok = true;
	if (compareVersions(tag, kCurrentVersion) > 0 && info.assetUrl.empty())
		info.error = "The latest release has no Windows executable asset.";
	return info;
}

InstallResult downloadAndInstall(const UpdateInfo& info) {
	InstallResult result;
	if (!info.ok || !info.available || info.assetUrl.empty()) {
		result.error = "No downloadable update is available.";
		return result;
	}
	const std::wstring target = modulePath();
	const std::wstring download = tempPath(L".exe");
	const std::wstring helper = tempPath(L".ps1");
	if (target.empty() || download.empty() || helper.empty()) {
		result.error = "Could not determine a temporary update path.";
		return result;
	}
	if (!downloadToFile(info.assetUrl, download, result.error) || !validExecutable(download, result.error)) {
		DeleteFileW(download.c_str());
		return result;
	}
	if (!launchReplacement(download, target, helper, result.error)) {
		DeleteFileW(download.c_str());
		DeleteFileW(helper.c_str());
		return result;
	}
	result.ok = true;
	return result;
}

bool selfTest(std::string& error) {
	const std::string json = R"({"tag_name":"v1.2.3","assets":[{"name":"notes.txt","browser_download_url":"https://example.invalid/notes.txt"},{"name":"RTSSReaderMacros.exe","browser_download_url":"https://example.invalid/RTSSReaderMacros.exe"}]})";
	if (normalizeVersion(readJsonString(json, "tag_name")) != "1.2.3" ||
		compareVersions("1.2.3", "1.2.2") <= 0 ||
		releaseAssetUrl(json).empty()) {
		error = "Updater self-test failed.";
		return false;
	}
	return true;
}

} // namespace Updater
