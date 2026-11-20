using System;
using System.Collections.Generic;

namespace MaraeEngine.Events
{
    /// <summary>Interface marcadora para qualquer tipo de dado de evento customizado.</summary>
    public interface IEventData { }

    /// <summary>
    /// Barramento de eventos genérico e fracamente acoplado. Scripts podem subscrever,
    /// desubscrever e disparar eventos de qualquer tipo que implemente <see cref="IEventData"/>.
    /// </summary>
    public static class EventSystem
    {
        private static readonly Dictionary<Type, Delegate> s_handlers = new();

        /// <summary>
        /// Subscreve um handler a um evento de tipo T.
        /// </summary>
        /// <typeparam name="T">Tipo de dados do evento.</typeparam>
        /// <param name="handler">Função a ser chamada quando o evento for disparado.</param>
        public static void Subscribe<T>(Action<T> handler) where T : IEventData
        {
            if (handler == null) throw new ArgumentNullException(nameof(handler));
            Type key = typeof(T);
            if (!s_handlers.TryGetValue(key, out Delegate? existing))
                s_handlers[key] = handler;
            else
                s_handlers[key] = Delegate.Combine(existing, handler);
        }

        /// <summary>Remove uma assinatura previamente registrada.</summary>
        public static void Unsubscribe<T>(Action<T> handler) where T : IEventData
        {
            if (handler == null) return;
            Type key = typeof(T);
            if (!s_handlers.TryGetValue(key, out Delegate? existing) || existing == null) return;
            Delegate? result = Delegate.Remove(existing, handler);
            if (result == null) s_handlers.Remove(key);
            else s_handlers[key] = result;
        }

        /// <summary>
        /// Dispara um evento, chamando todos os handlers subsccritos na ordem de inscrição.
        /// Exceções em handlers individuais são capturadas e não cancelam a propagação.
        /// </summary>
        public static void Trigger<T>(T args) where T : IEventData
        {
            if (!s_handlers.TryGetValue(typeof(T), out Delegate? del) || del == null) return;
            if (del is Action<T> act)
            {
                foreach (Action<T> handler in act.GetInvocationList())
                {
                    try { handler(args); }
                    catch (Exception ex)
                    {
                        Internal.NativeDelegates.Log?.Invoke(3,
                            $"[EventSystem] Handler de {typeof(T).Name} lançou: {ex.Message}");
                    }
                }
            }
        }

        /// <summary>Remove todos os handlers de todos os eventos.</summary>
        public static void ClearAll() => s_handlers.Clear();
    }

    /// <summary>Evento disparado após uma cena ser totalmente carregada.</summary>
    public sealed class SceneLoadedEvent : IEventData
    {
        /// <summary>Nome ou caminho da cena carregada.</summary>
        public string ScenePath { get; }

        /// <summary>Cria um novo evento de cena carregada.</summary>
        public SceneLoadedEvent(string scenePath)
        {
            ScenePath = scenePath ?? string.Empty;
        }
    }

    /// <summary>Evento disparado quando o estado Play é ligado ou desligado.</summary>
    public sealed class PlayStateChangedEvent : IEventData
    {
        /// <summary>True se entrou em Play, false se saiu.</summary>
        public bool IsPlaying { get; }

        /// <summary>Cria um novo evento de PlayState.</summary>
        public PlayStateChangedEvent(bool isPlaying)
        {
            IsPlaying = isPlaying;
        }
    }

    /// <summary>Evento disparado logo após uma nova entidade ser criada na cena.</summary>
    public sealed class EntityCreatedEvent : IEventData
    {
        /// <summary>ID 64-bit da entidade criada.</summary>
        public ulong EntityId { get; }

        /// <summary>Cria um novo evento de entidade criada.</summary>
        public EntityCreatedEvent(ulong entityId)
        {
            EntityId = entityId;
        }
    }

    /// <summary>Evento disparado imediatamente antes de uma entidade ser destruída.</summary>
    public sealed class EntityDestroyedEvent : IEventData
    {
        /// <summary>ID 64-bit da entidade a ser destruída.</summary>
        public ulong EntityId { get; }

        /// <summary>Cria um novo evento de destruição.</summary>
        public EntityDestroyedEvent(ulong entityId)
        {
            EntityId = entityId;
        }
    }
}
