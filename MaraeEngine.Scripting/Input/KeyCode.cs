namespace MaraeEngine
{
    /// <summary>
    /// Códigos de tecla compatíveis com GLFW usados pelo <see cref="Input"/>.
    /// Valores numéricos seguem estritamente a enumeração GLFW_KEY_*.
    /// </summary>
    public enum KeyCode
    {
        /// <summary>Tecla desconhecida.</summary>
        Unknown = -1,

        /// <summary>Espaço (GLFW_KEY_SPACE = 32).</summary>
        Space = 32,

        /// <summary>Aspas simples / apóstrofo.</summary>
        Apostrophe = 39,

        /// <summary>Vírgula.</summary>
        Comma = 44,
        /// <summary>Sinal de menos / hífen.</summary>
        Minus = 45,
        /// <summary>Ponto / ponto final.</summary>
        Period = 46,
        /// <summary>Barra / divisão.</summary>
        Slash = 47,

        /// <summary>Dígito 0.</summary>
        D0 = 48,
        /// <summary>Dígito 1.</summary>
        D1 = 49,
        /// <summary>Dígito 2.</summary>
        D2 = 50,
        /// <summary>Dígito 3.</summary>
        D3 = 51,
        /// <summary>Dígito 4.</summary>
        D4 = 52,
        /// <summary>Dígito 5.</summary>
        D5 = 53,
        /// <summary>Dígito 6.</summary>
        D6 = 54,
        /// <summary>Dígito 7.</summary>
        D7 = 55,
        /// <summary>Dígito 8.</summary>
        D8 = 56,
        /// <summary>Dígito 9.</summary>
        D9 = 57,

        /// <summary>Ponto e vírgula.</summary>
        Semicolon = 59,
        /// <summary>Sinal de igual.</summary>
        Equal = 61,

        /// <summary>Letra A.</summary>
        A = 65,
        /// <summary>Letra B.</summary>
        B = 66,
        /// <summary>Letra C.</summary>
        C = 67,
        /// <summary>Letra D.</summary>
        D = 68,
        /// <summary>Letra E.</summary>
        E = 69,
        /// <summary>Letra F.</summary>
        F = 70,
        /// <summary>Letra G.</summary>
        G = 71,
        /// <summary>Letra H.</summary>
        H = 72,
        /// <summary>Letra I.</summary>
        I = 73,
        /// <summary>Letra J.</summary>
        J = 74,
        /// <summary>Letra K.</summary>
        K = 75,
        /// <summary>Letra L.</summary>
        L = 76,
        /// <summary>Letra M.</summary>
        M = 77,
        /// <summary>Letra N.</summary>
        N = 78,
        /// <summary>Letra O.</summary>
        O = 79,
        /// <summary>Letra P.</summary>
        P = 80,
        /// <summary>Letra Q.</summary>
        Q = 81,
        /// <summary>Letra R.</summary>
        R = 82,
        /// <summary>Letra S.</summary>
        S = 83,
        /// <summary>Letra T.</summary>
        T = 84,
        /// <summary>Letra U.</summary>
        U = 85,
        /// <summary>Letra V.</summary>
        V = 86,
        /// <summary>Letra W.</summary>
        W = 87,
        /// <summary>Letra X.</summary>
        X = 88,
        /// <summary>Letra Y.</summary>
        Y = 89,
        /// <summary>Letra Z.</summary>
        Z = 90,

        /// <summary>Colchete esquerdo.</summary>
        LeftBracket = 91,
        /// <summary>Barra invertida.</summary>
        Backslash = 92,
        /// <summary>Colchete direito.</summary>
        RightBracket = 93,
        /// <summary>Acento grave / crase.</summary>
        GraveAccent = 96,

        /// <summary>Escape.</summary>
        Escape = 256,
        /// <summary>Enter / Return.</summary>
        Enter = 257,
        /// <summary>Tab.</summary>
        Tab = 258,
        /// <summary>Backspace.</summary>
        Backspace = 259,
        /// <summary>Insert.</summary>
        Insert = 260,
        /// <summary>Delete.</summary>
        Delete = 261,
        /// <summary>Seta para a direita.</summary>
        ArrowRight = 262,
        /// <summary>Seta para a esquerda.</summary>
        ArrowLeft = 263,
        /// <summary>Seta para baixo.</summary>
        ArrowDown = 264,
        /// <summary>Seta para cima.</summary>
        ArrowUp = 265,
        /// <summary>Page Up.</summary>
        PageUp = 266,
        /// <summary>Page Down.</summary>
        PageDown = 267,
        /// <summary>Home.</summary>
        Home = 268,
        /// <summary>End.</summary>
        End = 269,
        /// <summary>Caps Lock.</summary>
        CapsLock = 280,
        /// <summary>Scroll Lock.</summary>
        ScrollLock = 281,
        /// <summary>Num Lock.</summary>
        NumLock = 282,
        /// <summary>Print Screen.</summary>
        PrintScreen = 283,
        /// <summary>Pause.</summary>
        Pause = 284,

        /// <summary>Tecla de função F1.</summary>
        F1 = 290,
        /// <summary>F2.</summary>
        F2 = 291,
        /// <summary>F3.</summary>
        F3 = 292,
        /// <summary>F4.</summary>
        F4 = 293,
        /// <summary>F5.</summary>
        F5 = 294,
        /// <summary>F6.</summary>
        F6 = 295,
        /// <summary>F7.</summary>
        F7 = 296,
        /// <summary>F8.</summary>
        F8 = 297,
        /// <summary>F9.</summary>
        F9 = 298,
        /// <summary>F10.</summary>
        F10 = 299,
        /// <summary>F11.</summary>
        F11 = 300,
        /// <summary>F12.</summary>
        F12 = 301,

        /// <summary>Tecla 0 do teclado numérico.</summary>
        Keypad0 = 320,
        /// <summary>Tecla 1 do teclado numérico.</summary>
        Keypad1 = 321,
        /// <summary>Tecla 2 do teclado numérico.</summary>
        Keypad2 = 322,
        /// <summary>Tecla 3 do teclado numérico.</summary>
        Keypad3 = 323,
        /// <summary>Tecla 4 do teclado numérico.</summary>
        Keypad4 = 324,
        /// <summary>Tecla 5 do teclado numérico.</summary>
        Keypad5 = 325,
        /// <summary>Tecla 6 do teclado numérico.</summary>
        Keypad6 = 326,
        /// <summary>Tecla 7 do teclado numérico.</summary>
        Keypad7 = 327,
        /// <summary>Tecla 8 do teclado numérico.</summary>
        Keypad8 = 328,
        /// <summary>Tecla 9 do teclado numérico.</summary>
        Keypad9 = 329,
        /// <summary>Ponto decimal do teclado numérico.</summary>
        KeypadDecimal = 330,
        /// <summary>Divisão do teclado numérico.</summary>
        KeypadDivide = 331,
        /// <summary>Multiplicação do teclado numérico.</summary>
        KeypadMultiply = 332,
        /// <summary>Subtração do teclado numérico.</summary>
        KeypadSubtract = 333,
        /// <summary>Adição do teclado numérico.</summary>
        KeypadAdd = 334,
        /// <summary>Enter do teclado numérico.</summary>
        KeypadEnter = 335,
        /// <summary>Igual do teclado numérico.</summary>
        KeypadEqual = 336,

        /// <summary>Shift esquerdo.</summary>
        LeftShift = 340,
        /// <summary>Control esquerdo.</summary>
        LeftControl = 341,
        /// <summary>Alt esquerdo.</summary>
        LeftAlt = 342,
        /// <summary>Super / Windows esquerdo.</summary>
        LeftSuper = 343,
        /// <summary>Shift direito.</summary>
        RightShift = 344,
        /// <summary>Control direito.</summary>
        RightControl = 345,
        /// <summary>Alt direito.</summary>
        RightAlt = 346,
        /// <summary>Super / Windows direito.</summary>
        RightSuper = 347,
        /// <summary>Menu de contexto.</summary>
        Menu = 348,

        /// <summary>Botão esquerdo do mouse (pseudo-código usado por GetMouseButton).</summary>
        MouseLeft = 0,
        /// <summary>Botão direito do mouse.</summary>
        MouseRight = 1,
        /// <summary>Botão do meio (scroll).</summary>
        MouseMiddle = 2,
        /// <summary>Botão extra 1 do mouse.</summary>
        MouseButton1 = 3,
        /// <summary>Botão extra 2 do mouse.</summary>
        MouseButton2 = 4,
        /// <summary>Botão extra 3 do mouse.</summary>
        MouseButton3 = 5,
        /// <summary>Botão extra 4 do mouse.</summary>
        MouseButton4 = 6,
        /// <summary>Botão extra 5 do mouse.</summary>
        MouseButton5 = 7,
        /// <summary>Botão 1 (alias para Left).</summary>
        Mouse1 = 0,
        /// <summary>Botão 2 (alias para Right).</summary>
        Mouse2 = 1,
        /// <summary>Botão 3 (alias para Middle).</summary>
        Mouse3 = 2
    }
}
