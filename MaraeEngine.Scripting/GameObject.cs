namespace MaraeEngine
{
    /// <summary>
    /// Fachada estática estilo Unity com métodos utilitários para busca, criação e destruição de
    /// entidades. Espelha os membros estáticos equivalentes de <see cref="Entity"/>.
    /// </summary>
    public static class GameObject
    {
        /// <summary>Encontra a primeira entidade com o nome especificado.</summary>
        /// <param name="name">Nome da entidade a buscar.</param>
        /// <returns>Entidade encontrada ou null.</returns>
        public static Entity? Find(string name) => Entity.Find(name);

        /// <summary>Encontra a primeira entidade com a tag especificada.</summary>
        /// <param name="tag">Tag a buscar.</param>
        /// <returns>Entidade ou null.</returns>
        public static Entity? FindWithTag(string tag) => Entity.FindWithTag(tag);

        /// <summary>Retorna todas as entidades com a tag informada.</summary>
        /// <param name="tag">Tag a buscar.</param>
        /// <returns>Array (possivelmente vazio) de entidades correspondentes.</returns>
        public static Entity[] FindGameObjectsWithTag(string tag) => Entity.FindGameObjectsWithTag(tag);

        /// <summary>Busca uma entidade por seu identificador 64-bit único.</summary>
        /// <param name="id">ID da entidade.</param>
        /// <returns>Entidade ou null.</returns>
        public static Entity? FindEntityByID(ulong id) => Entity.FindEntityByID(id);

        /// <summary>Cria uma cópia da entidade fornecida e retorna a nova instância.</summary>
        /// <param name="source">Entidade fonte a ser clonada.</param>
        /// <returns>Clone da entidade.</returns>
        public static Entity Instantiate(Entity source) => Entity.Instantiate(source);

        /// <summary>Destrói a entidade especificada no final do frame corrente.</summary>
        /// <param name="entity">Entidade a ser destruída.</param>
        public static void Destroy(Entity entity) => Entity.Destroy(entity);

        /// <summary>Destrói a entidade e toda a hierarquia de descendentes.</summary>
        public static void DestroyHierarchy(Entity entity) => entity?.DestroyHierarchy();
    }
}
