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

{{/*
Fully qualified name of the DNS-01 webhook deployment/service.
*/}}
{{- define "mydns-ng.webhookFullname" -}}
{{- printf "%s-webhook" (include "mydns-ng.fullname" .) | trunc 63 | trimSuffix "-" -}}
{{- end -}}

{{/*
Webhook container image (defaults to main mydns image which contains it).
*/}}
{{- define "mydns-ng.webhookImage" -}}
{{- if .Values.dns01.image.repository -}}
{{- $tag := default .Chart.AppVersion .Values.dns01.image.tag -}}
{{- printf "%s:%s" .Values.dns01.image.repository $tag -}}
{{- else -}}
{{- include "mydns-ng.image" . -}}
{{- end -}}
{{- end -}}

{{/*
Webhook service account name.
*/}}
{{- define "mydns-ng.webhookServiceAccount" -}}
{{- printf "%s-webhook" (include "mydns-ng.fullname" .) | trunc 63 | trimSuffix "-" -}}
{{- end -}}

{{/*
Webhook DB credentials secret name.
*/}}
{{- define "mydns-ng.webhookDbSecret" -}}
{{- if .Values.dns01.db.existingSecret -}}
{{- .Values.dns01.db.existingSecret -}}
{{- else -}}
{{- printf "%s-webhook-db" (include "mydns-ng.fullname" .) | trunc 63 | trimSuffix "-" -}}
{{- end -}}
{{- end -}}

{{/*
Names of the webhook PKI resources.
*/}}
{{- define "mydns-ng.webhookSelfSignedIssuer" -}}
{{ printf "%s-webhook-selfsign" (include "mydns-ng.fullname" .) }}
{{- end -}}
{{- define "mydns-ng.webhookRootCAIssuer" -}}
{{ printf "%s-webhook-ca" (include "mydns-ng.fullname" .) }}
{{- end -}}
{{- define "mydns-ng.webhookRootCACertificate" -}}
{{ printf "%s-webhook-ca" (include "mydns-ng.fullname" .) }}
{{- end -}}
{{- define "mydns-ng.webhookServingCertificate" -}}
{{ printf "%s-webhook-tls" (include "mydns-ng.fullname" .) }}
{{- end -}}
