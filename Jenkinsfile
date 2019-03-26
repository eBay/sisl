pipeline {
    agent any

    environment {
        PROJECT = 'sisl'
        CONAN_CHANNEL = 'testing'
        CONAN_USER = 'sisl'
        CONAN_PASS = credentials('CONAN_PASS')
    }

    stages {
        stage('Get Version') {
            steps {
                script {
                    TAG = sh(script: "grep -m 1 'version =' conanfile.py | awk '{print \$3}' | tr -d '\n' | tr -d '\"'", returnStdout: true)
                }
            }
        }

        stage('Build') {
            steps {
                withDockerRegistry([credentialsId: 'ecr', url: "https://ecr.vip.ebayc3.com"]) {
                    sh "docker build --rm --build-arg CONAN_USER=${CONAN_USER} --build-arg CONAN_PASS=${CONAN_PASS} --build-arg CONAN_CHANNEL=${CONAN_CHANNEL} -t ${PROJECT}-${TAG} ."
                }
            }
        }

        stage('Deploy') {
            when {
                branch "${CONAN_CHANNEL}/*"
            }
            steps {
                sh "docker run --rm ${PROJECT}-${TAG}"
                slackSend channel: '#conan-pkgs', message: "*${PROJECT}/${TAG}@${CONAN_USER}/${CONAN_CHANNEL}* has been uploaded to conan repo."
            }
        }
    }

    post {
        always {
            sh "docker rmi -f ${PROJECT}-${TAG}"
            sh "docker rm -f ${PROJECT}-${TAG}_coverage || true"
        }
    }
}
