#pragma once
// ---------------------------------------------------------------------------
// AudioDecoder — detecta formato e lê metadados de arquivos de áudio.
// Usa miniaudio para decodificação; não mantém estado.
// ---------------------------------------------------------------------------
#include "AudioClip.h"
#include <string>

class AudioDecoder
{
public:
    // Detecta o formato pelo magic bytes do arquivo (não depende de extensão)
    static AudioFormat DetectFormat(const std::string& path);

    // Preenche os metadados do clip lendo o cabeçalho do arquivo.
    // Retorna false se o arquivo não puder ser aberto ou decodificado.
    static bool Inspect(AudioClip& clip);

    // Retorna o LoadMode adequado baseado no tamanho do arquivo.
    // < 256 KB → Memory; >= 256 KB → Stream
    static AudioLoadMode RecommendLoadMode(const std::string& path);

    // Converte o formato interno do miniaudio para AudioFormat
    static AudioFormat FromMiniAudioFormat(uint32_t maFormat);
};
