#!/bin/bash

# =============================================================================
# Canary Deployment Script for Translation Server
#
# Implements progressive rollout strategy:
# 1. Deploy new version to canary instances (5% traffic)
# 2. Monitor metrics and error rates
# 3. Gradually increase traffic (25% → 50% → 100%)
# 4. Automatic rollback on degradation
# =============================================================================

set -e

# =============================================================================
# Configuration
# =============================================================================

# Docker image settings
IMAGE_NAME="${IMAGE_NAME:-translation-server}"
IMAGE_TAG="${IMAGE_TAG:-latest}"
REGISTRY="${REGISTRY:-}"

# Deployment settings
NAMESPACE="${NAMESPACE:-default}"
DEPLOYMENT_NAME="${DEPLOYMENT_NAME:-translation-server}"
SERVICE_NAME="${SERVICE_NAME:-translation-service}"

# Canary settings
CANARY_WEIGHT_INITIAL=5
CANARY_WEIGHT_STAGES=(5 25 50 100)
CANARY_WAIT_TIME=300  # seconds between stages (5 minutes)

# Health check settings
HEALTH_ENDPOINT="${HEALTH_ENDPOINT:-/health}"
METRICS_ENDPOINT="${METRICS_ENDPOINT:-/metrics}"
MAX_ERROR_RATE=0.05  # 5%
MAX_LATENCY_INCREASE=0.20  # 20%

# Rollback settings
AUTO_ROLLBACK="${AUTO_ROLLBACK:-true}"
ROLLBACK_THRESHOLD_ERRORS=10

# Logging
LOG_FILE="canary_deploy_$(date +%Y%m%d_%H%M%S).log"

# =============================================================================
# Logging Functions
# =============================================================================

log() {
    local level=$1
    shift
    local message="$*"
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    echo "[$timestamp] [$level] $message" | tee -a "$LOG_FILE"
}

log_info() { log "INFO" "$@"; }
log_warn() { log "WARN" "$@"; }
log_error() { log "ERROR" "$@"; }
log_success() { log "SUCCESS" "$@"; }

# =============================================================================
# Prerequisite Checks
# =============================================================================

check_prerequisites() {
    log_info "Checking prerequisites..."

    # Check kubectl
    if ! command -v kubectl &> /dev/null; then
        log_error "kubectl not found. Please install kubectl."
        exit 1
    fi

    # Check docker
    if ! command -v docker &> /dev/null; then
        log_error "docker not found. Please install docker."
        exit 1
    fi

    # Check jq for JSON parsing
    if ! command -v jq &> /dev/null; then
        log_warn "jq not found. Installing might be required for metrics parsing."
    fi

    # Verify kubernetes connection
    if ! kubectl cluster-info &> /dev/null; then
        log_error "Cannot connect to Kubernetes cluster."
        exit 1
    fi

    log_success "Prerequisites check passed"
}

# =============================================================================
# Docker Image Functions
# =============================================================================

build_image() {
    log_info "Building Docker image: ${IMAGE_NAME}:${IMAGE_TAG}"

    docker build -t "${IMAGE_NAME}:${IMAGE_TAG}" -f Dockerfile .

    if [ -n "$REGISTRY" ]; then
        local full_image="${REGISTRY}/${IMAGE_NAME}:${IMAGE_TAG}"
        docker tag "${IMAGE_NAME}:${IMAGE_TAG}" "$full_image"
        docker push "$full_image"
        log_success "Image pushed to registry: $full_image"
    fi

    log_success "Docker image built successfully"
}

# =============================================================================
# Kubernetes Deployment Functions
# =============================================================================

create_canary_deployment() {
    local weight=$1
    log_info "Creating canary deployment with ${weight}% traffic..."

    # Generate canary deployment YAML
    cat <<EOF | kubectl apply -f -
apiVersion: apps/v1
kind: Deployment
metadata:
  name: ${DEPLOYMENT_NAME}-canary
  namespace: ${NAMESPACE}
  labels:
    app: ${DEPLOYMENT_NAME}
    version: canary
spec:
  replicas: 1
  selector:
    matchLabels:
      app: ${DEPLOYMENT_NAME}
      version: canary
  template:
    metadata:
      labels:
        app: ${DEPLOYMENT_NAME}
        version: canary
    spec:
      containers:
      - name: translation-server
        image: ${REGISTRY}${REGISTRY:+/}${IMAGE_NAME}:${IMAGE_TAG}
        ports:
        - containerPort: 8080
        env:
        - name: DEPLOYMENT_TYPE
          value: "canary"
        - name: MODEL_VERSION
          value: "${IMAGE_TAG}"
        readinessProbe:
          httpGet:
            path: ${HEALTH_ENDPOINT}
            port: 8080
          initialDelaySeconds: 30
          periodSeconds: 10
        livenessProbe:
          httpGet:
            path: ${HEALTH_ENDPOINT}
            port: 8080
          initialDelaySeconds: 60
          periodSeconds: 30
        resources:
          requests:
            memory: "2Gi"
            cpu: "1"
          limits:
            memory: "4Gi"
            cpu: "2"
            nvidia.com/gpu: 1
EOF

    log_success "Canary deployment created"
}

update_traffic_split() {
    local canary_weight=$1
    local stable_weight=$((100 - canary_weight))

    log_info "Updating traffic split: stable=${stable_weight}%, canary=${canary_weight}%"

    # Using Istio VirtualService for traffic splitting
    # Modify this section based on your service mesh (Istio/Linkerd/etc.)
    cat <<EOF | kubectl apply -f -
apiVersion: networking.istio.io/v1beta1
kind: VirtualService
metadata:
  name: ${SERVICE_NAME}
  namespace: ${NAMESPACE}
spec:
  hosts:
  - ${SERVICE_NAME}
  http:
  - route:
    - destination:
        host: ${SERVICE_NAME}
        subset: stable
      weight: ${stable_weight}
    - destination:
        host: ${SERVICE_NAME}
        subset: canary
      weight: ${canary_weight}
EOF

    # Also create/update DestinationRule
    cat <<EOF | kubectl apply -f -
apiVersion: networking.istio.io/v1beta1
kind: DestinationRule
metadata:
  name: ${SERVICE_NAME}
  namespace: ${NAMESPACE}
spec:
  host: ${SERVICE_NAME}
  subsets:
  - name: stable
    labels:
      version: stable
  - name: canary
    labels:
      version: canary
EOF

    log_success "Traffic split updated"
}

# =============================================================================
# Monitoring Functions
# =============================================================================

get_error_rate() {
    local deployment=$1

    # Get error rate from Prometheus/metrics endpoint
    # This is a simplified example - adapt based on your monitoring setup
    local pod_name=$(kubectl get pods -n "$NAMESPACE" -l "app=${DEPLOYMENT_NAME},version=${deployment}" -o jsonpath='{.items[0].metadata.name}' 2>/dev/null)

    if [ -z "$pod_name" ]; then
        echo "0"
        return
    fi

    # Query metrics endpoint
    local metrics=$(kubectl exec -n "$NAMESPACE" "$pod_name" -- curl -s "localhost:8080${METRICS_ENDPOINT}" 2>/dev/null || echo "")

    if [ -n "$metrics" ] && command -v jq &> /dev/null; then
        local error_rate=$(echo "$metrics" | jq -r '.error_rate // 0' 2>/dev/null || echo "0")
        echo "$error_rate"
    else
        echo "0"
    fi
}

get_average_latency() {
    local deployment=$1

    local pod_name=$(kubectl get pods -n "$NAMESPACE" -l "app=${DEPLOYMENT_NAME},version=${deployment}" -o jsonpath='{.items[0].metadata.name}' 2>/dev/null)

    if [ -z "$pod_name" ]; then
        echo "0"
        return
    fi

    local metrics=$(kubectl exec -n "$NAMESPACE" "$pod_name" -- curl -s "localhost:8080${METRICS_ENDPOINT}" 2>/dev/null || echo "")

    if [ -n "$metrics" ] && command -v jq &> /dev/null; then
        local latency=$(echo "$metrics" | jq -r '.avg_latency_ms // 0' 2>/dev/null || echo "0")
        echo "$latency"
    else
        echo "0"
    fi
}

check_canary_health() {
    log_info "Checking canary health..."

    # Wait for pods to be ready
    local ready=$(kubectl get pods -n "$NAMESPACE" -l "app=${DEPLOYMENT_NAME},version=canary" -o jsonpath='{.items[0].status.conditions[?(@.type=="Ready")].status}' 2>/dev/null)

    if [ "$ready" != "True" ]; then
        log_warn "Canary pods not ready"
        return 1
    fi

    # Check error rate
    local canary_error_rate=$(get_error_rate "canary")
    local stable_error_rate=$(get_error_rate "stable")

    log_info "Error rates - Canary: ${canary_error_rate}, Stable: ${stable_error_rate}"

    # Compare with threshold
    if (( $(echo "$canary_error_rate > $MAX_ERROR_RATE" | bc -l 2>/dev/null || echo "0") )); then
        log_error "Canary error rate (${canary_error_rate}) exceeds threshold (${MAX_ERROR_RATE})"
        return 1
    fi

    # Check latency
    local canary_latency=$(get_average_latency "canary")
    local stable_latency=$(get_average_latency "stable")

    log_info "Latency - Canary: ${canary_latency}ms, Stable: ${stable_latency}ms"

    if [ "$stable_latency" != "0" ]; then
        local latency_increase=$(echo "scale=2; ($canary_latency - $stable_latency) / $stable_latency" | bc -l 2>/dev/null || echo "0")

        if (( $(echo "$latency_increase > $MAX_LATENCY_INCREASE" | bc -l 2>/dev/null || echo "0") )); then
            log_error "Canary latency increase (${latency_increase}) exceeds threshold (${MAX_LATENCY_INCREASE})"
            return 1
        fi
    fi

    log_success "Canary health check passed"
    return 0
}

# =============================================================================
# Rollback Functions
# =============================================================================

rollback() {
    local reason=$1
    log_error "Rolling back canary deployment. Reason: $reason"

    # Remove canary deployment
    kubectl delete deployment "${DEPLOYMENT_NAME}-canary" -n "$NAMESPACE" --ignore-not-found=true

    # Reset traffic to 100% stable
    update_traffic_split 0

    log_warn "Rollback completed. All traffic restored to stable version."
}

# =============================================================================
# Promotion Functions
# =============================================================================

promote_canary() {
    log_info "Promoting canary to stable..."

    # Update stable deployment with canary image
    kubectl set image deployment/"${DEPLOYMENT_NAME}" \
        -n "$NAMESPACE" \
        translation-server="${REGISTRY}${REGISTRY:+/}${IMAGE_NAME}:${IMAGE_TAG}"

    # Wait for rollout
    kubectl rollout status deployment/"${DEPLOYMENT_NAME}" -n "$NAMESPACE" --timeout=300s

    # Remove canary deployment
    kubectl delete deployment "${DEPLOYMENT_NAME}-canary" -n "$NAMESPACE" --ignore-not-found=true

    # Reset traffic split
    update_traffic_split 0

    log_success "Canary promoted to stable successfully"
}

# =============================================================================
# Main Deployment Flow
# =============================================================================

canary_deploy() {
    log_info "Starting canary deployment for ${IMAGE_NAME}:${IMAGE_TAG}"

    # Build image if needed
    if [ "${BUILD_IMAGE:-false}" = "true" ]; then
        build_image
    fi

    # Create canary deployment
    create_canary_deployment $CANARY_WEIGHT_INITIAL

    # Wait for canary to be ready
    log_info "Waiting for canary deployment to be ready..."
    kubectl rollout status deployment/"${DEPLOYMENT_NAME}-canary" -n "$NAMESPACE" --timeout=300s || {
        rollback "Canary deployment failed to start"
        exit 1
    }

    # Progressive traffic increase
    for weight in "${CANARY_WEIGHT_STAGES[@]}"; do
        log_info "=== Stage: ${weight}% traffic to canary ==="

        update_traffic_split "$weight"

        # Monitor for configured wait time
        local monitor_end=$(($(date +%s) + CANARY_WAIT_TIME))

        while [ $(date +%s) -lt $monitor_end ]; do
            if ! check_canary_health; then
                if [ "$AUTO_ROLLBACK" = "true" ]; then
                    rollback "Health check failed at ${weight}% traffic"
                    exit 1
                else
                    log_warn "Health check failed but auto-rollback is disabled"
                fi
            fi

            log_info "Health check passed. Waiting... ($(($monitor_end - $(date +%s)))s remaining)"
            sleep 30
        done

        log_success "Stage ${weight}% completed successfully"

        # If we've reached 100%, promote
        if [ "$weight" -eq 100 ]; then
            promote_canary
            break
        fi
    done

    log_success "Canary deployment completed successfully!"
}

# =============================================================================
# Quick Rollback (for emergency use)
# =============================================================================

quick_rollback() {
    log_warn "Executing quick rollback..."
    rollback "Manual rollback requested"
    log_success "Quick rollback completed"
}

# =============================================================================
# Status Check
# =============================================================================

check_status() {
    log_info "Checking deployment status..."

    echo ""
    echo "=== Stable Deployment ==="
    kubectl get deployment "${DEPLOYMENT_NAME}" -n "$NAMESPACE" -o wide 2>/dev/null || echo "Not found"

    echo ""
    echo "=== Canary Deployment ==="
    kubectl get deployment "${DEPLOYMENT_NAME}-canary" -n "$NAMESPACE" -o wide 2>/dev/null || echo "Not found"

    echo ""
    echo "=== Pods ==="
    kubectl get pods -n "$NAMESPACE" -l "app=${DEPLOYMENT_NAME}" -o wide

    echo ""
    echo "=== Traffic Split ==="
    kubectl get virtualservice "${SERVICE_NAME}" -n "$NAMESPACE" -o jsonpath='{.spec.http[0].route}' 2>/dev/null | jq '.' || echo "VirtualService not found"
}

# =============================================================================
# Help
# =============================================================================

show_help() {
    cat <<EOF
Canary Deployment Script for Translation Server

Usage: $0 [command] [options]

Commands:
    deploy      Start canary deployment (default)
    rollback    Quick rollback to stable version
    status      Check current deployment status
    help        Show this help message

Environment Variables:
    IMAGE_NAME              Docker image name (default: translation-server)
    IMAGE_TAG               Docker image tag (default: latest)
    REGISTRY                Docker registry URL
    NAMESPACE               Kubernetes namespace (default: default)
    DEPLOYMENT_NAME         Deployment name (default: translation-server)
    BUILD_IMAGE             Build Docker image before deploy (true/false)
    AUTO_ROLLBACK           Enable automatic rollback (default: true)
    CANARY_WAIT_TIME        Wait time between stages in seconds (default: 300)

Examples:
    # Deploy with default settings
    $0 deploy

    # Deploy with custom image tag
    IMAGE_TAG=v2.0.0 $0 deploy

    # Deploy and build image
    BUILD_IMAGE=true $0 deploy

    # Quick rollback
    $0 rollback

    # Check status
    $0 status
EOF
}

# =============================================================================
# Main Entry Point
# =============================================================================

main() {
    local command=${1:-deploy}

    case $command in
        deploy)
            check_prerequisites
            canary_deploy
            ;;
        rollback)
            check_prerequisites
            quick_rollback
            ;;
        status)
            check_status
            ;;
        help|--help|-h)
            show_help
            ;;
        *)
            log_error "Unknown command: $command"
            show_help
            exit 1
            ;;
    esac
}

# Run main function with all arguments
main "$@"
