using System;
using MaraeEngine.Serialization;

namespace MaraeEngine
{
    /// <summary>
    /// Classe base abstrata para scripts de usuÃ¡rio. HerdÃ¡-la para criar comportamentos customizados
    /// e anexar a entidades via <c>ScriptComponent</c>. ExpÃµe ciclo de vida estilo MonoBehaviour.
    /// </summary>
    public abstract class ScriptBehaviour
    {
        /// <summary>Entidade Ã  qual este script estÃ¡ anexado.</summary>
        public Entity? gameObject;

        /// <summary>Entidade Ã  qual este script estÃ¡ anexado (alias para gameObject).</summary>
        public Entity? GameObject => gameObject;

        /// <summary>Transform da entidade associada (atalho).</summary>
        public Transform? transform => gameObject?.transform;
        public Rigidbody? rigidbody => gameObject == null ? null : new Rigidbody(gameObject.Handle);

        /// <summary>Construtor protegido (classe abstrata).</summary>
        protected ScriptBehaviour() { }

        #region Ciclo de vida (virtuais vazios - override quando necessÃ¡rio)

        /// <summary>Chamado imediatamente apÃ³s o script ser habilitado ou a entidade ser ativada.</summary>
        public virtual void OnEnable() { }

        /// <summary>Chamado imediatamente antes do script ser desabilitado ou a entidade desativada.</summary>
        public virtual void OnDisable() { }

        /// <summary>Chamado uma Ãºnica vez, antes do primeiro <see cref="OnUpdate"/>.</summary>
        public virtual void OnStart() { }

        /// <summary>Chamado uma Ãºnica vez (estilo Unity Start).</summary>
        public virtual void Start() { }

        /// <summary>Chamado a cada frame enquanto o script estÃ¡ habilitado e no modo Play.</summary>
        /// <param name="dt">Delta time decorrido desde o Ãºltimo frame (segundos).</param>
        public virtual void OnUpdate(float dt) { }

        /// <summary>Chamado no passo fixo da física (padrão: 60 Hz), antes da simulação física.</summary>
        public virtual void OnFixedUpdate(float fixedDt) { }

        /// <summary>Alias sem parâmetro para scripts que preferem consultar Time.fixedDeltaTime.</summary>
        public virtual void FixedUpdate() { }

        /// <summary>Chamado a cada frame (estilo Unity Update).</summary>
        public virtual void Update() { }

        /// <summary>Chamado ao destruir a instÃ¢ncia do script (sair do Play ou destruir entidade).</summary>
        public virtual void OnDestroy() { }

        #endregion

        #region Collision Stubs

        /// <summary>Chamado quando o collider deste objeto comeÃ§a a colidir com outro collider.</summary>
        /// <param name="collision">InformaÃ§Ãµes sobre a colisÃ£o.</param>
        public virtual void OnCollisionEnter(Collision.Stubs.Collision collision) { }

        /// <summary>Chamado a cada frame enquanto o collider permanece colidindo.</summary>
        public virtual void OnCollisionStay(Collision.Stubs.Collision collision) { }

        /// <summary>Chamado quando a colisÃ£o termina.</summary>
        public virtual void OnCollisionExit(Collision.Stubs.Collision collision) { }

        /// <summary>Chamado quando um trigger comeÃ§a a sobrepor este objeto.</summary>
        public virtual void OnTriggerEnter(Collision.Stubs.Collider other) { }

        /// <summary>Chamado a cada frame enquanto a sobreposiÃ§Ã£o de trigger permanece.</summary>
        public virtual void OnTriggerStay(Collision.Stubs.Collider other) { }

        /// <summary>Chamado quando a sobreposiÃ§Ã£o do trigger termina.</summary>
        public virtual void OnTriggerExit(Collision.Stubs.Collider other) { }

        #endregion

        #region UI Stubs

        /// <summary>Chamado quando um elemento de UI (botão) anexado a este script recebe um clique.</summary>
        /// <param name="element">Elemento de UI que disparou o evento.</param>
        public virtual void OnUIClicked(UIElement element) { }

        /// <summary>Chamado quando um Slider/Checkbox/Dropdown anexado altera seu valor.</summary>
        /// <param name="element">Elemento de UI que disparou o evento.</param>
        /// <param name="value">Novo valor do elemento.</param>
        public virtual void OnUIValueChanged(UIElement element, float value) { }

        /// <summary>Chamado quando um TextBox anexado tem seu texto alterado.</summary>
        /// <param name="element">Elemento de UI que disparou o evento.</param>
        /// <param name="text">Novo texto do elemento.</param>
        public virtual void OnUITextChanged(UIElement element, string text) { }

        #endregion

        #region Invoke e métodos personalizados

        /// <summary>Invoca um mÃ©todo do script apÃ³s <paramref name="time"/> segundos.</summary>
        /// <param name="methodName">Nome do mÃ©todo a invocar.</param>
        /// <param name="time">Atraso em segundos.</param>
        public void Invoke(string methodName, float time)
        {
            Internal.ScriptInvoker.ScheduleInvoke(this, methodName, time, 0.0f);
        }

        /// <summary>Invoca repetidamente um mÃ©todo a cada <paramref name="repeatRate"/> segundos, apÃ³s um atraso inicial.</summary>
        public void InvokeRepeating(string methodName, float time, float repeatRate)
        {
            Internal.ScriptInvoker.ScheduleInvoke(this, methodName, time, repeatRate);
        }

        /// <summary>Cancela todas as invocaÃ§Ãµes pendentes deste script.</summary>
        public void CancelInvoke()
        {
            Internal.ScriptInvoker.CancelInvoke(this, null);
        }

        /// <summary>Cancela as invocaÃ§Ãµes pendentes de um mÃ©todo especÃ­fico.</summary>
        public void CancelInvoke(string methodName)
        {
            Internal.ScriptInvoker.CancelInvoke(this, methodName);
        }

        /// <summary>Retorna verdadeiro se existe alguma invocaÃ§Ã£o pendente.</summary>
        public bool IsInvoking() => Internal.ScriptInvoker.IsInvoking(this, null);

        /// <summary>Retorna verdadeiro se o mÃ©todo especÃ­fico tem invocaÃ§Ã£o pendente.</summary>
        public bool IsInvoking(string methodName) => Internal.ScriptInvoker.IsInvoking(this, methodName);

        /// <summary>
        /// Invoca explicitamente qualquer método de instância do script, inclusive métodos
        /// personalizados que não fazem parte do ciclo de vida. Métodos podem ter parâmetros
        /// compatíveis com os argumentos fornecidos e retornar qualquer valor.
        /// </summary>
        public object? Call(string methodName, params object?[] args)
        {
            return Internal.ScriptInvoker.InvokeMethod(this, methodName, args);
        }

        #endregion
    }
}
