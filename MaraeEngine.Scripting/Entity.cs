using System;
using System.Runtime.InteropServices;
using MaraeEngine.Internal;

namespace MaraeEngine
{
    /// <summary>
    /// Representa um objeto na cena (entidade ECS). Encapsula um handle opaco para o lado nativo
    /// e expõe propriedades e operações comuns (nome, tag, ativo, camada, componentes, etc.).
    /// </summary>
    public class Entity : IEquatable<Entity>
    {
        private readonly ulong _handle;
        private Transform? _cachedTransform;
        private ulong? _cachedId;

        internal ulong Handle => _handle;

        /// <summary>Identificador único 64-bit da entidade.</summary>
        public ulong id
        {
            get
            {
                _cachedId ??= NativeDelegates.EntityGetID?.Invoke(_handle) ?? 0ul;
                return _cachedId.Value;
            }
        }

        /// <summary>Nome da entidade visível no editor.</summary>
        public string name
        {
            get
            {
                IntPtr ptr = NativeDelegates.EntityGetName?.Invoke(_handle) ?? IntPtr.Zero;
                if (ptr == IntPtr.Zero) return string.Empty;
                return Marshal.PtrToStringUni(ptr) ?? string.Empty;
            }
            set => NativeDelegates.EntitySetName?.Invoke(_handle, value ?? string.Empty);
        }

        /// <summary>Se a entidade está ativa (processada no pipeline de update/render).</summary>
        public bool activeSelf
        {
            get => NativeDelegates.EntityGetActive?.Invoke(_handle) ?? false;
            set => NativeDelegates.EntitySetActive?.Invoke(_handle, value);
        }

        /// <summary>Tag textual usada para busca e categorização.</summary>
        public string tag
        {
            get
            {
                IntPtr ptr = NativeDelegates.EntityGetTag?.Invoke(_handle) ?? IntPtr.Zero;
                if (ptr == IntPtr.Zero) return string.Empty;
                return Marshal.PtrToStringUni(ptr) ?? string.Empty;
            }
            set => NativeDelegates.EntitySetTag?.Invoke(_handle, value ?? string.Empty);
        }

        /// <summary>
        /// Camada de profundidade/z-order da entidade.
        /// Valores menores = mais ao fundo (renderizado primeiro); maiores = mais na frente.
        /// Aceita qualquer valor inteiro (infinitas camadas).
        /// </summary>
        public int layer
        {
            get => NativeDelegates.EntityGetLayer?.Invoke(_handle) ?? 0;
            set => NativeDelegates.EntitySetLayer?.Invoke(_handle, value);
        }

        /// <summary>Componente transform da entidade (todas as entidades têm, não pode ser nulo).</summary>
        public Transform transform
        {
            get
            {
                if (_cachedTransform == null)
                {
                    ulong tHandle = NativeDelegates.EntityGetTransform?.Invoke(_handle) ?? 0ul;
                    _cachedTransform = new Transform(tHandle);
                }
                return _cachedTransform;
            }
        }

        /// <summary>Indica se esta entidade possui um elemento UI 2D real da cena.</summary>
        public bool HasUI => NativeDelegates.EntityHasUI?.Invoke(_handle) ?? false;

        /// <summary>Acesso ao componente UI 2D desta entidade.</summary>
        public UIElement? ui => HasUI ? new UIElement(this) : null;

        /// <summary>Física da entidade. O wrapper existe mesmo quando a simulação está desativada.</summary>
        public Rigidbody rigidbody => new(_handle);

        /// <summary>Câmera de cena anexada, se houver.</summary>
        public CameraComponent? camera => GetComponent<CameraComponent>();

        /// <summary>Estado de animação da entidade.</summary>
        public AnimationComponent animation => new(_handle);

        /// <summary>Configurações de iluminação global, somente em entidades Global Light.</summary>
        public GlobalLightComponent? globalLight => GetComponent<GlobalLightComponent>();

        /// <summary>Construtor interno.</summary>
        /// <param name="handle">Handle nativo da entidade.</param>
        internal Entity(ulong handle)
        {
            _handle = handle;
        }

        /// <summary>
        /// Verifica se a entidade possui um componente do tipo T.
        /// </summary>
        /// <typeparam name="T">Componente suportado pelo SDK Marae.</typeparam>
        public bool HasComponent<T>()
            where T : class
        {
            int type = ComponentTypeMap<T>.Id;
            return NativeDelegates.EntityHasComponent?.Invoke(_handle, type) ?? false;
        }

        /// <summary>
        /// Obtém o componente T anexado à entidade, ou null se não existir.
        /// </summary>
        /// <typeparam name="T">Tipo do componente.</typeparam>
        public T? GetComponent<T>()
            where T : class
        {
            int type = ComponentTypeMap<T>.Id;
            ulong compHandle = NativeDelegates.EntityGetComponent?.Invoke(_handle, type) ?? 0ul;
            if (compHandle == 0ul) return null;
            Func<ulong, T>? factory = ComponentFactory<T>.Create;
            if (factory == null) return null;
            return factory(compHandle);
        }

        #region Static Find API

        /// <summary>Encontra a primeira entidade com o nome especificado (case-sensitive).</summary>
        public static Entity? Find(string name)
        {
            if (string.IsNullOrEmpty(name)) return null;
            ulong handle = NativeDelegates.EntityFindByName?.Invoke(name) ?? 0ul;
            return handle == 0ul ? null : new Entity(handle);
        }

        /// <summary>Encontra a primeira entidade com a tag especificada.</summary>
        public static Entity? FindWithTag(string tag)
        {
            if (string.IsNullOrEmpty(tag)) return null;
            ulong handle = NativeDelegates.EntityFindByTag?.Invoke(tag) ?? 0ul;
            return handle == 0ul ? null : new Entity(handle);
        }

        /// <summary>Encontra todas as entidades que contêm a tag especificada.</summary>
        public static Entity[] FindGameObjectsWithTag(string tag)
        {
            if (string.IsNullOrEmpty(tag)) return Array.Empty<Entity>();
            int count = 0;
            IntPtr ptr = IntPtr.Zero;
            if (NativeDelegates.EntityFindAllByTag != null)
                ptr = NativeDelegates.EntityFindAllByTag(tag, out count);
            if (count <= 0 || ptr == IntPtr.Zero) return Array.Empty<Entity>();
            Entity[] result = new Entity[count];
            unsafe
            {
                ulong* handles = (ulong*)ptr.ToPointer();
                for (int i = 0; i < count; i++)
                    result[i] = new Entity(handles[i]);
            }
            return result;
        }

        /// <summary>Encontra uma entidade por seu ID único 64-bit.</summary>
        public static Entity? FindEntityByID(ulong id)
        {
            ulong handle = NativeDelegates.EntityFindById?.Invoke(id) ?? 0ul;
            return handle == 0ul ? null : new Entity(handle);
        }

        /// <summary>Destrói uma entidade na cena no final do frame corrente.</summary>
        public static void Destroy(Entity entity)
        {
            if (entity == null) return;
            NativeDelegates.EntityDestroy?.Invoke(entity._handle);
        }

        /// <summary>Destrói esta entidade.</summary>
        public void Destroy() => Destroy(this);

        /// <summary>Destrói esta entidade e todos os descendentes no fim do frame.</summary>
        public void DestroyHierarchy()
        {
            NativeDelegates.EntityDestroyHierarchy?.Invoke(_handle);
        }

        /// <summary>Clona uma entidade (incluindo seus componentes) e retorna a nova instância.</summary>
        public static Entity Instantiate(Entity source)
        {
            if (source == null)
                throw new ArgumentNullException(nameof(source));
            ulong handle = NativeDelegates.EntityInstantiate?.Invoke(source._handle) ?? 0ul;
            return new Entity(handle);
        }

        #endregion

        #region IEquatable

        /// <inheritdoc/>
        public bool Equals(Entity? other)
        {
            if (other is null) return false;
            if (ReferenceEquals(this, other)) return true;
            return _handle == other._handle;
        }

        /// <inheritdoc/>
        public override bool Equals(object? obj)
        {
            return obj is Entity e && Equals(e);
        }

        /// <inheritdoc/>
        public override int GetHashCode() => _handle.GetHashCode();

        /// <summary>Compara se duas entidades representam o mesmo objeto nativo.</summary>
        public static bool operator ==(Entity? a, Entity? b)
        {
            if (ReferenceEquals(a, b)) return true;
            if (a is null || b is null) return false;
            return a._handle == b._handle;
        }

        /// <summary>Compara desigualdade entre entidades.</summary>
        public static bool operator !=(Entity? a, Entity? b) => !(a == b);

        /// <inheritdoc/>
        public override string ToString()
        {
            string n = name;
            return string.IsNullOrEmpty(n) ? $"Entity #{id}" : n;
        }

        #endregion
    }

    /// <summary>
    /// Mapeia tipos de componente C# para o inteiro esperado pelo binding nativo (ComponentType enum no C++).
    /// </summary>
    /// <typeparam name="T">Tipo do componente.</typeparam>
    internal static class ComponentTypeMap<T>
    {
        // Ordem alinhada com o enum ComponentType no lado C++:
        // Keep in sync with ComponentType in Engine/Scripting/ScriptingAPI.h.
        public static readonly int Id = typeof(T) switch
        {
            Type t when t == typeof(MeshComponent) => ComponentIds.Mesh,
            Type t when t == typeof(AudioComponent) => ComponentIds.Audio,
            Type t when t == typeof(LightComponent) => ComponentIds.Light,
            Type t when t == typeof(MaterialComponent) => ComponentIds.Material,
            Type t when t == typeof(ScriptComponent) => ComponentIds.Script,
            Type t when t == typeof(Rigidbody) => ComponentIds.Physics,
            Type t when t == typeof(UIElement) => ComponentIds.UI,
            Type t when t == typeof(AnimationComponent) => ComponentIds.Animation,
            Type t when t == typeof(CameraComponent) => ComponentIds.Camera,
            Type t when t == typeof(GlobalLightComponent) => ComponentIds.GlobalLight,
            _ => 0
        };
    }

    /// <summary>
    /// Cria uma instância da fachada gerenciada T para o componente identificado pelo handle nativo.
    /// </summary>
    internal static class ComponentFactory<T> where T : class
    {
        public static readonly Func<ulong, T>? Create = BuildFactory();

        private static Func<ulong, T>? BuildFactory()
        {
            Type t = typeof(T);
            if (t == typeof(MeshComponent)) return (h => (T)(object)new MeshComponent(h));
            if (t == typeof(AudioComponent)) return (h => (T)(object)new AudioComponent(h));
            if (t == typeof(LightComponent)) return (h => (T)(object)new LightComponent(h));
            if (t == typeof(MaterialComponent)) return (h => (T)(object)new MaterialComponent(h));
            if (t == typeof(ScriptComponent)) return (h => (T)(object)new ScriptComponent(h));
            if (t == typeof(Rigidbody)) return (h => (T)(object)new Rigidbody(h));
            if (t == typeof(UIElement)) return (h => (T)(object)new UIElement(new Entity(h)));
            if (t == typeof(AnimationComponent)) return (h => (T)(object)new AnimationComponent(h));
            if (t == typeof(CameraComponent)) return (h => (T)(object)new CameraComponent(h));
            if (t == typeof(GlobalLightComponent)) return (h => (T)(object)new GlobalLightComponent(h));
            return null;
        }
    }
}
