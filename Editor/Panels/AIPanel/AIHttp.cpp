#include "AIHttp.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>

#include <string>

static std::string WinHttpErrorMessage(const char* operation, DWORD code)
{
    const char* detail = "erro desconhecido";
    switch (code)
    {
    case ERROR_WINHTTP_TIMEOUT:          detail = "tempo limite excedido"; break;
    case ERROR_WINHTTP_NAME_NOT_RESOLVED: detail = "servidor nao encontrado"; break;
    case ERROR_WINHTTP_CANNOT_CONNECT:   detail = "nao foi possivel conectar ao servidor"; break;
    case ERROR_WINHTTP_CONNECTION_ERROR: detail = "conexao interrompida"; break;
    case ERROR_WINHTTP_SECURE_FAILURE:   detail = "falha TLS/HTTPS"; break;
    case ERROR_WINHTTP_INVALID_URL:      detail = "URL invalida"; break;
    case ERROR_WINHTTP_CLIENT_AUTH_CERT_NEEDED: detail = "certificado do cliente necessario"; break;
    default: break;
    }

    return std::string(operation) + " (WinHTTP " + std::to_string(code) +
        ": " + detail + ").";
}

#pragma comment(lib, "winhttp.lib")

static std::wstring Utf8ToWide(const std::string& text)
{
    if (text.empty())
        return L"";
    const int count = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0);
    if (count <= 0)
        return L"";
    std::wstring wide((size_t)count, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), wide.data(), count);
    return wide;
}

static bool ParseUrl(const std::string& url, bool& https, std::wstring& host, std::wstring& path, INTERNET_PORT& port)
{
    https = true;
    port = INTERNET_DEFAULT_HTTPS_PORT;
    std::string rest = url;

    if (rest.rfind("https://", 0) == 0)
    {
        https = true;
        rest = rest.substr(8);
        port = INTERNET_DEFAULT_HTTPS_PORT;
    }
    else if (rest.rfind("http://", 0) == 0)
    {
        https = false;
        rest = rest.substr(7);
        port = INTERNET_DEFAULT_HTTP_PORT;
    }

    const size_t slash = rest.find('/');
    std::string hostPort = slash == std::string::npos ? rest : rest.substr(0, slash);
    path = Utf8ToWide(slash == std::string::npos ? "/" : rest.substr(slash));

    const size_t colon = hostPort.find(':');
    if (colon != std::string::npos)
    {
        host = Utf8ToWide(hostPort.substr(0, colon));
        try
        {
            port = (INTERNET_PORT)std::stoi(hostPort.substr(colon + 1));
        }
        catch (...)
        {
            return false;
        }
    }
    else
    {
        host = Utf8ToWide(hostPort);
    }

    return !host.empty() && !path.empty();
}

std::string AIHttpPostJson(
    const std::string& url,
    const std::string& bearerToken,
    const std::string& jsonBody,
    std::string& error)
{
    bool https = true;
    std::wstring host;
    std::wstring path;
    INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
    if (!ParseUrl(url, https, host, path, port))
    {
        error = "Endpoint da API invalido.";
        return {};
    }

    HINTERNET session = WinHttpOpen(
        L"MaraeEngine-AIPanel/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (!session)
    {
        error = "Nao foi possivel iniciar o cliente HTTP.";
        return {};
    }

    // O contexto da engine pode ser grande e modelos atuais podem levar mais
    // de um minuto para responder. Mantemos conexao/envio curtos, mas damos
    // tempo suficiente para o processamento da resposta.
    WinHttpSetTimeouts(session, 30000, 30000, 30000, 180000);

    HINTERNET connect = WinHttpConnect(session, host.c_str(), port, 0);
    if (!connect)
    {
        WinHttpCloseHandle(session);
        error = "Nao foi possivel conectar ao servidor da API.";
        return {};
    }

    DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(
        connect,
        L"POST",
        path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags);
    if (!request)
    {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        error = "Nao foi possivel abrir a requisicao HTTP.";
        return {};
    }

    std::wstring headers = L"Content-Type: application/json\r\n";
    if (!bearerToken.empty())
        headers += L"Authorization: Bearer " + Utf8ToWide(bearerToken) + L"\r\n";

    BOOL ok = WinHttpSendRequest(
        request,
        headers.c_str(),
        (DWORD)-1,
        (LPVOID)jsonBody.data(),
        (DWORD)jsonBody.size(),
        (DWORD)jsonBody.size(),
        0);

    if (!ok)
    {
        const DWORD code = GetLastError();
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        error = WinHttpErrorMessage("Falha ao enviar a requisicao", code);
        return {};
    }

    if (!WinHttpReceiveResponse(request, nullptr))
    {
        const DWORD code = GetLastError();
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        error = WinHttpErrorMessage("Falha ao receber a resposta da API", code);
        return {};
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    WinHttpQueryHeaders(
        request,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &status,
        &statusSize,
        WINHTTP_NO_HEADER_INDEX);

    std::string response;
    DWORD available = 0;
    do
    {
        available = 0;
        if (!WinHttpQueryDataAvailable(request, &available))
            break;
        if (available == 0)
            break;

        std::string chunk(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request, chunk.data(), available, &read))
            break;
        chunk.resize(read);
        response += chunk;
    } while (available > 0);

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);

    if (status >= 400)
    {
        error = "HTTP " + std::to_string(status);
        if (!response.empty())
        {
            error += ": ";
            error += response.size() > 500 ? response.substr(0, 500) : response;
        }
        return {};
    }

    return response;
}

std::string AIGetExecutablePath()
{
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    return exePath;
}

std::string AIGetAppDataPath()
{
    char path[MAX_PATH] = {};
    const DWORD n = GetEnvironmentVariableA("APPDATA", path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return {};
    return path;
}
