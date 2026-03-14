{{/*
Expand the name of the chart.
*/}}
{{- define "mydns-ng.name" -}}
{{- default .Chart.Name .Values.nameOverride | trunc 63 | trimSuffix "-" }}
{{- end }}

{{/*
Create a default fully qualified app name.
*/}}
{{- define "mydns-ng.fullname" -}}
{{- if .Values.fullnameOverride }}
{{- .Values.fullnameOverride | trunc 63 | trimSuffix "-" }}
{{- else }}
{{- $name := default .Chart.Name .Values.nameOverride }}
{{- if contains $name .Release.Name }}
{{- .Release.Name | trunc 63 | trimSuffix "-" }}
{{- else }}
{{- printf "%s-%s" .Release.Name $name | trunc 63 | trimSuffix "-" }}
{{- end }}
{{- end }}
{{- end }}

{{/*
Create chart name and version as used by the chart label.
*/}}
{{- define "mydns-ng.chart" -}}
{{- printf "%s-%s" .Chart.Name .Chart.Version | replace "+" "_" | trunc 63 | trimSuffix "-" }}
{{- end }}

{{/*
Common labels
*/}}
{{- define "mydns-ng.labels" -}}
helm.sh/chart: {{ include "mydns-ng.chart" . }}
{{ include "mydns-ng.selectorLabels" . }}
{{- if .Chart.AppVersion }}
app.kubernetes.io/version: {{ .Chart.AppVersion | quote }}
{{- end }}
app.kubernetes.io/managed-by: {{ .Release.Service }}
{{- end }}

{{/*
Selector labels
*/}}
{{- define "mydns-ng.selectorLabels" -}}
app.kubernetes.io/name: {{ include "mydns-ng.name" . }}
app.kubernetes.io/instance: {{ .Release.Name }}
{{- end }}

{{/*
Create the name of the service account to use
*/}}
{{- define "mydns-ng.serviceAccountName" -}}
{{- if .Values.serviceAccount.create }}
{{- default (include "mydns-ng.fullname" .) .Values.serviceAccount.name }}
{{- else }}
{{- default "default" .Values.serviceAccount.name }}
{{- end }}
{{- end }}

{{/*
Create image name with tag
*/}}
{{- define "mydns-ng.image" -}}
{{- $tag := default .Chart.AppVersion .Values.image.tag }}
{{- printf "%s:%s" .Values.image.repository $tag }}
{{- end }}

{{/*
Database secret name
*/}}
{{- define "mydns-ng.databaseSecretName" -}}
{{- if .Values.database.existingSecret }}
{{- .Values.database.existingSecret }}
{{- else }}
{{- include "mydns-ng.fullname" . }}-db
{{- end }}
{{- end }}
