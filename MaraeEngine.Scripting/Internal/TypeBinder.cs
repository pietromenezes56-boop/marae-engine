using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using MaraeEngine.Serialization;

namespace MaraeEngine.Internal
{
    /// <summary>
    /// Alias interno para <see cref="Serialization.TypeBinder"/>, mantendo a consistência do namespace
    /// esperado por <c>Bootstrap.cs</c> quando ele resolve nomes de tipo.
    /// </summary>
    internal static class ScriptTypeBinder
    {
        /// <inheritdoc cref="Serialization.TypeBinder.DiscoverScriptBehaviourTypes"/>
        public static IReadOnlyList<Type> Discover(System.Reflection.Assembly assembly)
            => Serialization.TypeBinder.DiscoverScriptBehaviourTypes(assembly);

        /// <inheritdoc cref="Serialization.TypeBinder.ExtractPublicFieldsAsJson"/>
        public static string Extract(object instance)
            => Serialization.TypeBinder.ExtractPublicFieldsAsJson(instance);

        /// <inheritdoc cref="Serialization.TypeBinder.ApplySingleFieldFromJson"/>
        public static bool Apply(object instance, string fieldName, string jsonValue)
            => Serialization.TypeBinder.ApplySingleFieldFromJson(instance, fieldName, jsonValue);
    }
}
