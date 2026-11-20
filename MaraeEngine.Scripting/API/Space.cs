namespace MaraeEngine
{
    /// <summary>
    /// Define o espaço de referência para operações de transform (rotação, translação).
    /// </summary>
    public enum Space
    {
        /// <summary>
        /// Operação aplicada no espaço local do próprio objeto (relativo ao seu transform).
        /// </summary>
        Self = 0,

        /// <summary>
        /// Operação aplicada no espaço mundial (absoluto da cena).
        /// </summary>
        World = 1
    }
}
