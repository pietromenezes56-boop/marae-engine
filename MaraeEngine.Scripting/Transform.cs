using System;
using MaraeEngine.Internal;

namespace MaraeEngine
{
    /// <summary>
    /// Representa a transformação 3D (posição, rotação, escala) de uma entidade.
    /// Todos os getters/setters delegam para o lado nativo C++ via <see cref="NativeDelegates"/>.
    /// </summary>
    public class Transform
    {
        private readonly ulong _handle;

        internal ulong Handle => _handle;

        /// <summary>Construtor interno chamado apenas pelo runtime nativo.</summary>
        /// <param name="handle">Handle nativo do transform (entityId no C++).</param>
        internal Transform(ulong handle)
        {
            _handle = handle;
        }

        /// <summary>Posição local relativa ao transform pai.</summary>
        public Vector3 localPosition
        {
            get
            {
                if (NativeDelegates.TransformGetLocalPosition == null) return Vector3.zero;
                NativeDelegates.TransformGetLocalPosition(_handle, out var x, out var y, out var z);
                return new Vector3(x, y, z);
            }
            set
            {
                NativeDelegates.TransformSetLocalPosition?.Invoke(_handle, value.X, value.Y, value.Z);
            }
        }

        /// <summary>Posição mundial (absoluta na cena).</summary>
        public Vector3 position
        {
            get
            {
                if (NativeDelegates.TransformGetWorldPosition == null) return Vector3.zero;
                NativeDelegates.TransformGetWorldPosition(_handle, out var x, out var y, out var z);
                return new Vector3(x, y, z);
            }
            set
            {
                NativeDelegates.TransformSetWorldPosition?.Invoke(_handle, value.X, value.Y, value.Z);
            }
        }

        /// <summary>Rotação local relativa ao transform pai (quatérnio).</summary>
        public Quaternion localRotation
        {
            get
            {
                if (NativeDelegates.TransformGetLocalRotation == null) return Quaternion.identity;
                NativeDelegates.TransformGetLocalRotation(_handle, out var x, out var y, out var z, out var w);
                return new Quaternion(x, y, z, w);
            }
            set
            {
                NativeDelegates.TransformSetLocalRotation?.Invoke(_handle, value.x, value.y, value.z, value.w);
            }
        }

        /// <summary>Rotação mundial (absoluta na cena, quatérnio).</summary>
        public Quaternion rotation
        {
            get
            {
                if (NativeDelegates.TransformGetWorldRotation == null) return Quaternion.identity;
                NativeDelegates.TransformGetWorldRotation(_handle, out var x, out var y, out var z, out var w);
                return new Quaternion(x, y, z, w);
            }
            set
            {
                NativeDelegates.TransformSetWorldRotation?.Invoke(_handle, value.x, value.y, value.z, value.w);
            }
        }

        /// <summary>Escala local relativa ao transform pai.</summary>
        public Vector3 localScale
        {
            get
            {
                if (NativeDelegates.TransformGetLocalScale == null) return Vector3.one;
                NativeDelegates.TransformGetLocalScale(_handle, out var x, out var y, out var z);
                return new Vector3(x, y, z);
            }
            set
            {
                NativeDelegates.TransformSetLocalScale?.Invoke(_handle, value.X, value.Y, value.Z);
            }
        }

        /// <summary>Ângulos de Euler locais (radianos).</summary>
        public Vector3 localEulerAngles
        {
            get
            {
                if (NativeDelegates.TransformGetEuler == null) return Vector3.zero;
                NativeDelegates.TransformGetEuler(_handle, out var x, out var y, out var z);
                return new Vector3(x, y, z);
            }
            set
            {
                NativeDelegates.TransformSetEuler?.Invoke(_handle, value.X, value.Y, value.Z);
            }
        }

        /// <summary>Ângulos de Euler mundiais (radianos).</summary>
        public Vector3 eulerAngles
        {
            get => rotation.eulerAngles;
            set => rotation = Quaternion.Euler(value);
        }

        /// <summary>Direção "frente" mundial (eixo Z positivo rotacionado).</summary>
        public Vector3 forward
        {
            get => rotation * Vector3.forward;
            set
            {
                if (value.sqrMagnitude < 1e-8f) return;
                var f = value.normalized;
                rotation = Quaternion.LookRotation(f);
            }
        }

        /// <summary>Direção "direita" mundial (eixo X positivo rotacionado).</summary>
        public Vector3 right
        {
            get => rotation * Vector3.right;
        }

        /// <summary>Direção "cima" mundial (eixo Y positivo rotacionado).</summary>
        public Vector3 up
        {
            get => rotation * Vector3.up;
        }

        /// <summary>
        /// Translada o transform por um delta. Aplica em <see cref="Space.Self"/> por padrão.
        /// </summary>
        /// <param name="delta">Vetor de deslocamento.</param>
        /// <param name="relativeTo">Se aplicar em espaço local ou mundial.</param>
        public void Translate(Vector3 delta, Space relativeTo = Space.Self)
        {
            NativeDelegates.TransformTranslateDelta?.Invoke(_handle, delta.X, delta.Y, delta.Z, (int)relativeTo);
        }

        /// <summary>Sobrecarga de Translate com componentes separados.</summary>
        public void Translate(float x, float y, float z, Space relativeTo = Space.Self)
        {
            NativeDelegates.TransformTranslateDelta?.Invoke(_handle, x, y, z, (int)relativeTo);
        }

        /// <summary>
        /// Rotaciona o transform por ângulos de Euler (radianos) no espaço especificado.
        /// </summary>
        /// <param name="eulerAngles">Rotação por eixo em radianos.</param>
        /// <param name="relativeTo">Espaço Self ou World.</param>
        public void Rotate(Vector3 eulerAngles, Space relativeTo = Space.Self)
        {
            NativeDelegates.TransformRotateEuler?.Invoke(_handle, eulerAngles.X, eulerAngles.Y, eulerAngles.Z, (int)relativeTo);
        }

        /// <summary>Sobrecarga de Rotate com componentes separados.</summary>
        public void Rotate(float xAngle, float yAngle, float zAngle, Space relativeTo = Space.Self)
        {
            NativeDelegates.TransformRotateEuler?.Invoke(_handle, xAngle, yAngle, zAngle, (int)relativeTo);
        }

        /// <summary>
        /// Rotaciona o transform ao redor de um ponto/arbitrário no mundo, com um eixo e ângulo (radianos).
        /// </summary>
        /// <param name="point">Ponto de pivô no mundo.</param>
        /// <param name="axis">Eixo de rotação.</param>
        /// <param name="angle">Ângulo em radianos.</param>
        public void RotateAround(Vector3 point, Vector3 axis, float angle)
        {
            if (axis.sqrMagnitude < 1e-10f) return;
            Vector3 dir = position - point;
            Quaternion q = Quaternion.AngleAxis(angle, axis.normalized);
            Vector3 newDir = q * dir;
            position = point + newDir;
            rotation = q * rotation;
        }

        /// <summary>
        /// Faz o transform "olhar" para um alvo (eixo forward alinhado com alvo).
        /// </summary>
        /// <param name="target">Ponto alvo no mundo.</param>
        public void LookAt(Vector3 target)
        {
            NativeDelegates.TransformLookAt?.Invoke(_handle, target.X, target.Y, target.Z);
        }

        /// <summary>
        /// Define o pai da entidade, opcionalmente preservando a transformação mundial.
        /// </summary>
        /// <param name="parent">Entidade pai (valor nulo remove o pai).</param>
        /// <param name="worldPositionStays">Se true, preserva posição/rotação/escala mundial.</param>
        public void SetParent(Entity? parent, bool worldPositionStays = true)
        {
            ulong parentHandle = parent == null ? 0ul : parent.Handle;
            NativeDelegates.TransformSetParent?.Invoke(_handle, parentHandle, worldPositionStays);
        }

        /// <summary>Número de filhos diretos.</summary>
        public int childCount
        {
            get => NativeDelegates.TransformGetChildCount?.Invoke(_handle) ?? 0;
        }

        /// <summary>
        /// Retorna o transform de um filho por índice.
        /// </summary>
        /// <param name="index">Índice do filho [0, childCount-1].</param>
        /// <exception cref="ArgumentOutOfRangeException">Se índice fora do intervalo.</exception>
        public Transform GetChild(int index)
        {
            if (index < 0 || index >= childCount)
                throw new ArgumentOutOfRangeException(nameof(index));
            ulong childEntityHandle = NativeDelegates.TransformGetChild?.Invoke(_handle, index) ?? 0ul;
            ulong childTransformHandle = NativeDelegates.EntityGetTransform?.Invoke(childEntityHandle) ?? 0ul;
            return new Transform(childTransformHandle);
        }

        /// <summary>Retorna o filho como Entity, permitindo operar em sua hierarquia via C#.</summary>
        public Entity GetChildEntity(int index)
        {
            if (index < 0 || index >= childCount)
                throw new ArgumentOutOfRangeException(nameof(index));
            ulong childEntityHandle = NativeDelegates.TransformGetChild?.Invoke(_handle, index) ?? 0ul;
            if (childEntityHandle == 0ul)
                throw new InvalidOperationException("Filho inválido ou destruído.");
            return new Entity(childEntityHandle);
        }

        /// <summary>Transform pai, ou null se for raiz da cena.</summary>
        public Transform? parent
        {
            get
            {
                ulong parentEntityHandle = NativeDelegates.TransformGetParent?.Invoke(_handle) ?? 0ul;
                if (parentEntityHandle == 0ul) return null;
                ulong parentTransformHandle = NativeDelegates.EntityGetTransform?.Invoke(parentEntityHandle) ?? 0ul;
                return new Transform(parentTransformHandle);
            }
            set
            {
                NativeDelegates.TransformSetParent?.Invoke(_handle, value?._handle ?? 0ul, true);
            }
        }

        /// <summary>
        /// Camada de profundidade/z-order da entidade dono deste transform.
        /// Valores menores são renderizados mais ao fundo; valores maiores ficam na frente.
        /// Aceita qualquer valor inteiro (infinitas camadas).
        /// </summary>
        public int layer
        {
            get => NativeDelegates.EntityGetLayer?.Invoke(_handle) ?? 0;
            set => NativeDelegates.EntitySetLayer?.Invoke(_handle, value);
        }
    }
}
