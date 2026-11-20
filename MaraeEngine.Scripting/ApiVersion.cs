namespace MaraeEngine
{
    /// <summary>Contrato público de compatibilidade do SDK C# da Marae Engine.</summary>
    public static class MaraeApi
    {
        /// <summary>Versão major do contrato. Mudanças incompatíveis incrementam este número.</summary>
        public const int Major = 1;
        /// <summary>Versão minor do contrato. Recursos aditivos incrementam este número.</summary>
        public const int Minor = 0;
        /// <summary>Versão textual do contrato, útil para diagnósticos e scripts.</summary>
        public const string Version = "1.0";
        /// <summary>Política: membros existentes permanecem compatíveis durante a mesma major.</summary>
        public const string CompatibilityPolicy =
            "Mudanças aditivas preservam a API 1.x; remoções exigem guia de migração e nova major.";
    }
}
