using MaraeEngine.Internal;

namespace MaraeEngine
{
    /// <summary>
    /// API estática de entrada (teclado e mouse). Todos os valores são lidos diretamente do lado
    /// nativo via bindings C++. Valores de eixo são normalizados [-1, 1] com deadzone padrão.
    /// </summary>
    public static class Input
    {
        private const float DeadZone = 0.19f;

        /// <summary>Retorna true enquanto a tecla <paramref name="key"/> está pressionada.</summary>
        public static bool GetKey(KeyCode key)
        {
            return NativeDelegates.InputGetKey?.Invoke((int)key) ?? false;
        }

        /// <summary>Retorna true EXATAMENTE no frame em que a tecla foi pressionada.</summary>
        public static bool GetKeyDown(KeyCode key)
        {
            return NativeDelegates.InputGetKeyDown?.Invoke((int)key) ?? false;
        }

        /// <summary>Retorna true EXATAMENTE no frame em que a tecla foi solta.</summary>
        public static bool GetKeyUp(KeyCode key)
        {
            return NativeDelegates.InputGetKeyUp?.Invoke((int)key) ?? false;
        }

        /// <summary>Alias de gameplay para GetKey.</summary>
        public static bool IsKeyDown(KeyCode key) => GetKey(key);

        /// <summary>Alias de gameplay para GetKeyDown.</summary>
        public static bool IsKeyPressed(KeyCode key) => GetKeyDown(key);

        /// <summary>Alias de gameplay para GetKeyUp.</summary>
        public static bool IsKeyReleased(KeyCode key) => GetKeyUp(key);

        /// <summary>Retorna true enquanto o botão do mouse está pressionado.</summary>
        /// <param name="button">0 = esquerdo, 1 = direito, 2 = meio, 3-7 = botões extra.</param>
        public static bool GetMouseButton(int button)
        {
            return NativeDelegates.InputGetMouseButton?.Invoke(button) ?? false;
        }

        /// <summary>Retorna true no frame exato em que o botão do mouse foi pressionado.</summary>
        public static bool GetMouseButtonDown(int button)
        {
            return NativeDelegates.InputGetMouseButtonDown?.Invoke(button) ?? false;
        }

        /// <summary>Retorna true no frame exato em que o botão do mouse foi solto.</summary>
        public static bool GetMouseButtonUp(int button)
        {
            return NativeDelegates.InputGetMouseButtonUp?.Invoke(button) ?? false;
        }

        /// <summary>
        /// Posição do cursor do mouse em pixels de tela (origem no canto superior-esquerdo).
        /// A componente Z é sempre 0.
        /// </summary>
        public static Vector3 mousePosition
        {
            get
            {
                float x = 0, y = 0, z = 0;
                NativeDelegates.InputGetMousePos?.Invoke(out x, out y, out z);
                return new Vector3(x, y, z);
            }
        }

        /// <summary>
        /// Retorna os eixos de teclado no intervalo [-1, 1]. Eixos suportados:
        /// <c>"Horizontal"</c> (A/D ou setas), <c>"Vertical"</c> (W/S ou setas),
        /// e <c>"Mouse X"</c>/<c>"Mouse Y"</c> (delta do cursor em pixels no frame).
        /// </summary>
        /// <param name="axisName">Nome do eixo.</param>
        public static float GetAxis(string axisName)
        {
            if (string.IsNullOrEmpty(axisName)) return 0f;
            float raw = NativeDelegates.InputGetAxis?.Invoke(axisName) ?? 0f;
            if (string.Equals(axisName, "Mouse X", StringComparison.OrdinalIgnoreCase) ||
                string.Equals(axisName, "Mouse Y", StringComparison.OrdinalIgnoreCase))
                return raw;
            if (System.Math.Abs(raw) < DeadZone) return 0f;
            float normalized = (System.Math.Abs(raw) - DeadZone) / (1f - DeadZone);
            return System.Math.Sign(raw) * System.Math.Clamp(normalized, 0f, 1f);
        }

        /// <summary>Valor bruto do eixo, sem aplicar deadzone.</summary>
        public static float GetAxisRaw(string axisName)
        {
            if (string.IsNullOrEmpty(axisName)) return 0f;
            return NativeDelegates.InputGetAxis?.Invoke(axisName) ?? 0f;
        }

        /// <summary>Consulta uma ação do mapa de entrada do projeto.</summary>
        public static bool GetMappedAction(string actionName)
        {
            if (string.IsNullOrWhiteSpace(actionName)) return false;
            return NativeDelegates.InputGetAction?.Invoke(actionName) ?? false;
        }

        /// <summary>Consulta a borda de pressionamento de uma ação mapeada.</summary>
        public static bool GetMappedActionDown(string actionName)
        {
            if (string.IsNullOrWhiteSpace(actionName)) return false;
            return NativeDelegates.InputGetActionDown?.Invoke(actionName) ?? false;
        }

        /// <summary>Consulta a borda de soltura de uma ação mapeada.</summary>
        public static bool GetMappedActionUp(string actionName)
        {
            if (string.IsNullOrWhiteSpace(actionName)) return false;
            return NativeDelegates.InputGetActionUp?.Invoke(actionName) ?? false;
        }

        /// <summary>Adiciona uma tecla ao mapa persistente do projeto.</summary>
        public static void BindKey(string actionName, KeyCode key, float scale = 1f)
        {
            if (!string.IsNullOrWhiteSpace(actionName))
                NativeDelegates.InputBindKey?.Invoke(actionName, (int)key, scale);
        }

        /// <summary>Adiciona um botão de mouse ao mapa persistente do projeto.</summary>
        public static void BindMouseButton(string actionName, int button, float scale = 1f)
        {
            if (!string.IsNullOrWhiteSpace(actionName))
                NativeDelegates.InputBindMouseButton?.Invoke(actionName, button, scale);
        }

        /// <summary>Remove todos os bindings de uma ação.</summary>
        public static void ClearBindings(string actionName)
        {
            if (!string.IsNullOrWhiteSpace(actionName))
                NativeDelegates.InputClearBindings?.Invoke(actionName);
        }

        /// <summary>Define a dead zone de um eixo entre 0 e 0,99.</summary>
        public static void SetAxisDeadZone(string axisName, float deadZone)
        {
            if (!string.IsNullOrWhiteSpace(axisName))
                NativeDelegates.InputSetDeadZone?.Invoke(axisName, deadZone);
        }

        /// <summary>
        /// Ação de gameplay para protótipos 2D. Jump/Flap/Submit aceitam
        /// Espaço, Enter, seta para cima ou clique esquerdo; Cancel aceita Escape.
        /// </summary>
        public static bool GetActionDown(string actionName)
        {
            if (string.IsNullOrWhiteSpace(actionName)) return false;
            if (NativeDelegates.InputGetActionDown != null)
                return GetMappedActionDown(actionName);
            string action = actionName.Trim().ToLowerInvariant();
            if (action == "jump" || action == "flap" || action == "submit")
                return GetKeyDown(KeyCode.Space) || GetKeyDown(KeyCode.Enter) ||
                       GetKeyDown(KeyCode.ArrowUp) || GetMouseButtonDown(0);
            if (action == "cancel")
                return GetKeyDown(KeyCode.Escape);
            return false;
        }

        /// <summary>Versão contínua de GetActionDown para ações mantidas.</summary>
        public static bool GetAction(string actionName)
        {
            if (string.IsNullOrWhiteSpace(actionName)) return false;
            if (NativeDelegates.InputGetAction != null)
                return GetMappedAction(actionName);
            string action = actionName.Trim().ToLowerInvariant();
            if (action == "jump" || action == "flap" || action == "submit")
                return GetKey(KeyCode.Space) || GetKey(KeyCode.Enter) ||
                       GetKey(KeyCode.ArrowUp) || GetMouseButton(0);
            if (action == "cancel")
                return GetKey(KeyCode.Escape);
            return false;
        }

        /// <summary>Botão esquerdo do mouse pressionado.</summary>
        public static bool leftMouse => GetMouseButton(0);
        /// <summary>Botão direito do mouse pressionado.</summary>
        public static bool rightMouse => GetMouseButton(1);
        /// <summary>Botão do meio pressionado.</summary>
        public static bool middleMouse => GetMouseButton(2);
    }
}
