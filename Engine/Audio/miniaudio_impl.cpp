// ---------------------------------------------------------------------------
// miniaudio implementation — translation unit isolada.
// Este arquivo define MINIAUDIO_IMPLEMENTATION uma única vez para evitar
// símbolos duplicados. Todos os outros arquivos incluem <miniaudio.h>
// sem a define.
// ---------------------------------------------------------------------------

// Habilita todos os decodificadores embutidos:
//   WAV  — nativo no miniaudio
//   MP3  — dr_mp3 embutido
//   FLAC — dr_flac embutido
//   OGG  — stb_vorbis embutido
#define MA_NO_ENCODING
#define MA_NO_GENERATION

#define MINIAUDIO_IMPLEMENTATION
#include "../../ThirdParty/miniaudio/miniaudio.h"
