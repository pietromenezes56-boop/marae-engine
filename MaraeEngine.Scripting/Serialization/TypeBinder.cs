using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Reflection;
using System.Text.Json;
using System.Text.Json.Nodes;
using MaraeEngine.Internal;
using MaraeEngine.Serialization;

namespace MaraeEngine.Serialization
{
    /// <summary>
    /// Serviço auxiliar para descoberta e serialização de campos públicos (ou com [SerializeField])
    /// de instâncias de <see cref="ScriptBehaviour"/>. Usado pelo Inspector e pelo hot-reload.
    /// </summary>
    public static class TypeBinder
    {
        private static readonly JsonSerializerOptions JsonOpts = new()
        {
            WriteIndented = false,
            PropertyNameCaseInsensitive = true,
            NumberHandling = System.Text.Json.Serialization.JsonNumberHandling.AllowReadingFromString |
                             System.Text.Json.Serialization.JsonNumberHandling.WriteAsString
        };

        /// <summary>
        /// Descobre todos os tipos de um assembly que herdam de <see cref="ScriptBehaviour"/>.
        /// </summary>
        /// <param name="assembly">Assembly a varrer.</param>
        /// <returns>Lista (nunca nula) dos tipos concretos que herdam ScriptBehaviour.</returns>
        public static IReadOnlyList<Type> DiscoverScriptBehaviourTypes(System.Reflection.Assembly assembly)
        {
            if (assembly == null) return Array.Empty<Type>();
            try
            {
                Type baseType = typeof(ScriptBehaviour);
                return assembly.GetTypes()
                    .Where(t => t.IsClass && !t.IsAbstract && !t.IsGenericType && baseType.IsAssignableFrom(t))
                    .ToList();
            }
            catch (Exception ex)
            {
                NativeDelegates.Log?.Invoke(2, $"[TypeBinder] Falha ao descobrir ScriptBehaviours: {ex.Message}");
                return Array.Empty<Type>();
            }
        }

        /// <summary>
        /// Extrai todos os campos serializáveis de uma instância em formato JSON (objeto { nome: valor }).
        /// </summary>
        /// <param name="instance">Instância de ScriptBehaviour.</param>
        /// <returns>String JSON compacta com os valores dos campos.</returns>
        public static string ExtractPublicFieldsAsJson(object instance)
        {
            if (instance == null) return "{}";
            JsonObject obj = new();
            IEnumerable<FieldInfo> fields = EnumerateSerializableFields(instance.GetType());
            foreach (FieldInfo f in fields)
            {
                object? val = f.GetValue(instance);
                JsonNode? node = SerializeValue(val, f.FieldType);
                if (node != null)
                    obj[f.Name] = node;
            }
            return obj.ToJsonString(JsonOpts);
        }

        /// <summary>
        /// Aplica um dicionário de campos (JSON) na instância alvo. Campos não reconhecidos são ignorados.
        /// </summary>
        /// <param name="instance">Instância alvo.</param>
        /// <param name="jsonFields">JSON no formato { "FieldName": value, ... }</param>
        public static void ApplyFieldsFromJson(object instance, string jsonFields)
        {
            if (instance == null || string.IsNullOrWhiteSpace(jsonFields)) return;
            try
            {
                JsonObject? obj = JsonNode.Parse(jsonFields)?.AsObject();
                if (obj == null) return;
                Dictionary<string, FieldInfo> map = EnumerateSerializableFields(instance.GetType())
                    .ToDictionary(f => f.Name, StringComparer.Ordinal);
                foreach (KeyValuePair<string, JsonNode?> kv in obj)
                {
                    if (map.TryGetValue(kv.Key, out FieldInfo? fi) && kv.Value != null)
                    {
                        object? converted = DeserializeValue(kv.Value, fi.FieldType);
                        if (converted != null || fi.FieldType.IsClass || Nullable.GetUnderlyingType(fi.FieldType) != null)
                        {
                            try { fi.SetValue(instance, converted); }
                            catch (Exception ex)
                            {
                                NativeDelegates.Log?.Invoke(2,
                                    $"[TypeBinder] Não foi possível definir campo {kv.Key}: {ex.Message}");
                            }
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                NativeDelegates.Log?.Invoke(3, $"[TypeBinder] Falha ao aplicar campos JSON: {ex.Message}");
            }
        }

        /// <summary>
        /// Aplica um único campo a partir de um valor JSON.
        /// </summary>
        public static bool ApplySingleFieldFromJson(object instance, string fieldName, string jsonValue)
        {
            if (instance == null || string.IsNullOrEmpty(fieldName)) return false;
            FieldInfo? fi = EnumerateSerializableFields(instance.GetType())
                .FirstOrDefault(f => string.Equals(f.Name, fieldName, StringComparison.Ordinal));
            if (fi == null) return false;
            try
            {
                JsonNode? node = JsonNode.Parse(jsonValue);
                if (node == null) return false;
                object? converted = DeserializeValue(node, fi.FieldType);
                fi.SetValue(instance, converted);
                return true;
            }
            catch (Exception ex)
            {
                NativeDelegates.Log?.Invoke(2,
                    $"[TypeBinder] ApplySingleFieldFromJson falhou ({fieldName}): {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// Enumera todos os campos serializáveis de um tipo (públicos OU com [SerializeField]).
        /// </summary>
        public static IEnumerable<FieldInfo> EnumerateSerializableFields(Type type)
        {
            if (type == null) yield break;
            BindingFlags flags = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;
            Type? current = type;
            while (current != null && current != typeof(object))
            {
                foreach (FieldInfo fi in current.GetFields(flags))
                {
                    if (fi.IsStatic) continue;
                    if (fi.IsPublic)
                    {
                        if (fi.IsLiteral || fi.IsInitOnly) continue;
                        yield return fi;
                    }
                    else
                    {
                        object[] attrs = fi.GetCustomAttributes(typeof(SerializeFieldAttribute), true);
                        if (attrs.Length > 0) yield return fi;
                    }
                }
                current = current.BaseType;
            }
        }

        /// <summary>Retorna uma representação leve dos metadados de campo (nome + tag de tipo).</summary>
        public static IReadOnlyList<FieldMetadata> DescribeFields(Type type)
        {
            List<FieldMetadata> result = new();
            foreach (FieldInfo fi in EnumerateSerializableFields(type))
            {
                result.Add(new FieldMetadata(fi.Name, TypeTagFor(fi.FieldType)));
            }
            return result;
        }

        private static JsonNode? SerializeValue(object? value, Type fieldType)
        {
            if (value == null) return null;
            if (fieldType == typeof(Vector3))
            {
                Vector3 v = (Vector3)value;
                return new JsonObject
                {
                    ["x"] = JsonValue.Create(v.X),
                    ["y"] = JsonValue.Create(v.Y),
                    ["z"] = JsonValue.Create(v.Z)
                };
            }
            if (fieldType == typeof(Quaternion))
            {
                Quaternion q = (Quaternion)value;
                return new JsonObject
                {
                    ["x"] = JsonValue.Create(q.x),
                    ["y"] = JsonValue.Create(q.y),
                    ["z"] = JsonValue.Create(q.z),
                    ["w"] = JsonValue.Create(q.w)
                };
            }
            if (fieldType == typeof(Color))
            {
                Color c = (Color)value;
                return new JsonObject
                {
                    ["r"] = JsonValue.Create(c.R),
                    ["g"] = JsonValue.Create(c.G),
                    ["b"] = JsonValue.Create(c.B),
                    ["a"] = JsonValue.Create(c.A)
                };
            }
            try { return JsonSerializer.SerializeToNode(value, fieldType, JsonOpts); }
            catch { return JsonValue.Create(value?.ToString() ?? string.Empty); }
        }

        private static object? DeserializeValue(JsonNode node, Type targetType)
        {
            try
            {
                if (targetType == typeof(Vector3))
                {
                    JsonObject o = node.AsObject();
                    return new Vector3(
                        (float?)o["x"] ?? 0f,
                        (float?)o["y"] ?? 0f,
                        (float?)o["z"] ?? 0f);
                }
                if (targetType == typeof(Quaternion))
                {
                    JsonObject o = node.AsObject();
                    return new Quaternion(
                        (float?)o["x"] ?? 0f,
                        (float?)o["y"] ?? 0f,
                        (float?)o["z"] ?? 0f,
                        (float?)o["w"] ?? 1f);
                }
                if (targetType == typeof(Color))
                {
                    JsonObject o = node.AsObject();
                    return new Color(
                        (float?)o["r"] ?? 0f,
                        (float?)o["g"] ?? 0f,
                        (float?)o["b"] ?? 0f,
                        (float?)o["a"] ?? 1f);
                }
                return node.Deserialize(targetType, JsonOpts);
            }
            catch
            {
                // Tenta fallback por String / ChangeType.
                string s = node.ToString();
                try { return Convert.ChangeType(s, Nullable.GetUnderlyingType(targetType) ?? targetType, CultureInfo.InvariantCulture); }
                catch { return targetType.IsValueType ? Activator.CreateInstance(targetType) : null; }
            }
        }

        /// <summary>Tag de tipo usada pelo Inspector para selecionar o widget adequado.</summary>
        public static string TypeTagFor(Type t)
        {
            if (t == typeof(int)) return "int";
            if (t == typeof(float)) return "float";
            if (t == typeof(double)) return "double";
            if (t == typeof(bool)) return "bool";
            if (t == typeof(string)) return "string";
            if (t == typeof(Vector3)) return "Vector3";
            if (t == typeof(Quaternion)) return "Quaternion";
            if (t == typeof(Color)) return "Color";
            return t.FullName ?? t.Name;
        }
    }

    /// <summary>
    /// Metadados leves de campo serializável.
    /// </summary>
    public sealed class FieldMetadata
    {
        /// <summary>Nome do campo (identificador C#).</summary>
        public string Name { get; }

        /// <summary>Tag de tipo (int/float/bool/string/Vector3/Color/etc.).</summary>
        public string TypeTag { get; }

        /// <summary>Cria novos metadados.</summary>
        public FieldMetadata(string name, string typeTag)
        {
            Name = name;
            TypeTag = typeTag;
        }
    }
}
